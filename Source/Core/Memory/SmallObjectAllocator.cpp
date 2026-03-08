// ============================================================================
// D-Engine - Source/Core/Memory/SmallObjectAllocator.cpp
// ----------------------------------------------------------------------------
// Purpose : Out-of-line implementation for the slab-backed small-object
//           allocator so public headers stay focused on contract and layout.
// Contract: Mirrors the header contract exactly; no exceptions/RTTI.
// Notes   : The hot paths still remain straightforward and auditable. This TU
//           only moves implementation weight out of the public include graph.
// ============================================================================

#include "Core/Memory/SmallObjectAllocator.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <new>
#include <thread>
#include <vector>

namespace dng::core
{

SmallObjectAllocator::SmallObjectAllocator(IAllocator* parent, SmallObjectConfig cfg)
    : mParent(parent), mCfg(cfg)
{
    DNG_CHECK(mParent != nullptr);
    DNG_CHECK(mCfg.SlabSizeBytes >= 4096);

    if (mParent == nullptr)
    {
        mShardCount = 0;
        mShards = nullptr;
        return;
    }

    const usize requestedBatch = (cfg.TLSBatchSize == 0) ? kDefaultBatch : cfg.TLSBatchSize;
    mBaseBatch = std::clamp(requestedBatch, usize{ 1 }, kMagazineCapacity);

#if DNG_SMALLOBJ_TLS_BINS
    mTLSBinsEnabled = cfg.EnableTLSBins;
#endif

    const usize requestedShards = (cfg.ShardCountOverride == 0u)
        ? kDefaultShardCount
        : cfg.ShardCountOverride;
    InitShards(requestedShards);
    mCfg.ShardCountOverride = mShardCount;
}

SmallObjectAllocator::~SmallObjectAllocator()
{
#if DNG_SMALLOBJ_TLS_BINS
    if (mTLSBinsEnabled)
    {
        ThreadCache& cache = TLSBins::Cache();
        if (cache.OwnerInstance == this)
        {
            FlushThreadCache(cache);
            cache.OwnerInstance = nullptr;
            cache.Reset(mBaseBatch);
        }
    }
#endif

    mAlive.store(false, std::memory_order_release);

    if (mShards != nullptr)
    {
        for (usize shardIdx = 0; shardIdx < mShardCount; ++shardIdx)
        {
            Shard& shard = mShards[shardIdx];
            std::scoped_lock shardGuard(shard.Mutex);
            for (i32 classIdx = 0; classIdx < kNumClasses; ++classIdx)
            {
                shard.FreeLists[classIdx] = nullptr;
            }
        }
    }

    for (i32 classIdx = 0; classIdx < kNumClasses; ++classIdx)
    {
        Class& klass = mClasses[classIdx];
        std::scoped_lock slabGuard(klass.SlabMutex);

        SlabHeader* slab = klass.Slabs;
        while (slab != nullptr)
        {
            SlabHeader* next = slab->Next;
            mParent->Deallocate(static_cast<void*>(slab), mCfg.SlabSizeBytes, alignof(std::max_align_t));
            slab = next;
        }

        klass.Slabs = nullptr;
        klass.SlabCount.store(0, std::memory_order_relaxed);
        klass.FreeCount.store(0, std::memory_order_relaxed);
        klass.CachedCount.store(0, std::memory_order_relaxed);
    }

    if (mShards != nullptr)
    {
        for (usize i = 0; i < mShardCount; ++i)
        {
            mShards[i].~Shard();
        }

        const usize bytes = mShardCount * sizeof(Shard);
        const usize alignment = alignof(Shard);
        mParent->Deallocate(mShards, bytes, alignment);

        mShards = nullptr;
        mShardCount = 0;
    }
}

void SmallObjectAllocator::OnThreadExit() noexcept
{
#if DNG_SMALLOBJ_TLS_BINS
    if (!mTLSBinsEnabled)
    {
        return;
    }
    TLSBins::FlushOnThreadExit(*this);
#endif
}

void* SmallObjectAllocator::Allocate(usize size, usize alignment) noexcept
{
    if (mParent == nullptr)
    {
        return nullptr;
    }

    alignment = NormalizeAlignment(alignment);
    if (size == 0)
    {
        size = 1;
    }

    if (mShards == nullptr || mShardCount == 0)
    {
        return mParent->Allocate(size, alignment);
    }

    if (size > mCfg.MaxClassSize)
    {
        return mParent->Allocate(size, alignment);
    }

    const ClassIndex ci = SizeToClass(size);
    if (ci.Index < 0)
    {
        return mParent->Allocate(size, alignment);
    }

    const usize supportedAlignment = NormalizeAlignment(NaturalAlignFor(kClassSizes[static_cast<usize>(ci.Index)]));
    if (alignment > supportedAlignment)
    {
        return mParent->Allocate(size, alignment);
    }

    return AllocateFromClass(ci, size, alignment);
}

void SmallObjectAllocator::Deallocate(void* ptr, usize size, usize alignment) noexcept
{
    if (ptr == nullptr || mParent == nullptr)
    {
        return;
    }

    alignment = NormalizeAlignment(alignment);
    if (size == 0)
    {
        size = 1;
    }

    if (mShards == nullptr || mShardCount == 0)
    {
        mParent->Deallocate(ptr, size, alignment);
        return;
    }

    if (size > mCfg.MaxClassSize)
    {
        mParent->Deallocate(ptr, size, alignment);
        return;
    }

    const ClassIndex ci = SizeToClass(size);
    if (ci.Index < 0)
    {
        mParent->Deallocate(ptr, size, alignment);
        return;
    }

    const usize supportedAlignment = NormalizeAlignment(NaturalAlignFor(kClassSizes[static_cast<usize>(ci.Index)]));
    if (alignment > supportedAlignment)
    {
        mParent->Deallocate(ptr, size, alignment);
        return;
    }

    auto* user = static_cast<u8*>(ptr);
    auto* header = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));
    SlabHeader* slab = header->OwnerSlab;

    DNG_CHECK(slab != nullptr);
    const i32 classIdx = slab->ClassIndex;
    DNG_CHECK(classIdx >= 0 && classIdx < kNumClasses);

    FreeBlock(ptr, classIdx);
}

void* SmallObjectAllocator::Reallocate(void* ptr,
    usize oldSize,
    usize newSize,
    usize alignment,
    bool* wasInPlace) noexcept
{
    if (mParent == nullptr)
    {
        if (wasInPlace)
        {
            *wasInPlace = false;
        }
        return nullptr;
    }

    alignment = NormalizeAlignment(alignment);

    if (wasInPlace)
    {
        *wasInPlace = false;
    }

    if (ptr == nullptr)
    {
        return Allocate(newSize, alignment);
    }

    if (oldSize == 0)
    {
        DNG_LOG_ERROR("Memory",
            "SmallObjectAllocator::Reallocate misuse: ptr=%p oldSize==0 (alignment=%zu, newSize=%zu)",
            ptr,
            static_cast<size_t>(alignment),
            static_cast<size_t>(newSize));
        DNG_ASSERT(false && "Reallocate requires original size when ptr != nullptr");
        return nullptr;
    }

    if (newSize == 0)
    {
        Deallocate(ptr, oldSize, alignment);
        return nullptr;
    }

    if (mShards == nullptr || mShardCount == 0)
    {
        return mParent->Reallocate(ptr, oldSize, newSize, alignment, wasInPlace);
    }

    void* newBlock = Allocate(newSize, alignment);
    if (newBlock == nullptr)
    {
        return nullptr;
    }

    const usize copySize = oldSize < newSize ? oldSize : newSize;
    if (copySize > 0)
    {
        std::memcpy(newBlock, ptr, copySize);
    }

    Deallocate(ptr, oldSize, alignment);
    return newBlock;
}

void SmallObjectAllocator::DumpStats(const char* category) const
{
    usize totalSlabs = 0;
    usize totalMem = 0;
    usize totalFree = 0;
    usize totalBlocks = 0;

    for (i32 i = 0; i < kNumClasses; ++i)
    {
        const Class& klass = mClasses[i];
        const usize blockSize = EffectiveUserBlockSize(i);
        const usize slabCount = klass.SlabCount.load(std::memory_order_relaxed);
        const usize freeCount = klass.FreeCount.load(std::memory_order_relaxed);
        const usize cachedCount = klass.CachedCount.load(std::memory_order_relaxed);
        const usize blocks = slabCount * BlocksPerSlab(i);

        totalSlabs += slabCount;
        totalBlocks += blocks;
        totalFree += (freeCount + cachedCount);
        totalMem += slabCount * mCfg.SlabSizeBytes;

        DNG_LOG_INFO(category,
            "[SmallObject] class=%d size=%zu bytes, slabs=%zu, blocks=%zu, free=%zu (tls=%zu)",
            static_cast<int>(i),
            static_cast<size_t>(blockSize),
            static_cast<size_t>(slabCount),
            static_cast<size_t>(blocks),
            static_cast<size_t>(freeCount),
            static_cast<size_t>(cachedCount));
    }

    DNG_LOG_INFO(category,
        "[SmallObject] TOTAL slabs=%zu, blocks=%zu, free=%zu, mem=%zu KB",
        static_cast<size_t>(totalSlabs),
        static_cast<size_t>(totalBlocks),
        static_cast<size_t>(totalFree),
        static_cast<size_t>(totalMem / 1024));
}

std::uint64_t SmallObjectAllocator::ThreadFingerprint() noexcept
{
#if DNG_SMALLOBJ_TLS_BINS
    return TLSBins::ThreadFingerprint();
#else
    thread_local const std::uint64_t value = []() noexcept {
        std::hash<std::thread::id> hasher;
        const std::uint64_t hashed = static_cast<std::uint64_t>(hasher(std::this_thread::get_id()));
        return hashed == 0ull ? 0x1ull : hashed;
    }();
    return value;
#endif
}

void SmallObjectAllocator::InitShards(usize requestedCount)
{
    const usize normalized = NormalizeShardCount(requestedCount);
    const usize bytes = normalized * sizeof(Shard);
    const usize alignment = alignof(Shard);

    if (mParent == nullptr)
    {
        mShardCount = 0;
        mShards = nullptr;
        return;
    }

    void* memory = mParent->Allocate(bytes, alignment);
    if (memory == nullptr)
    {
        if (!mCfg.ReturnNullOnOOM)
        {
            HandleOutOfMemory(bytes, alignment, "SmallObjectAllocator::InitShards");
        }
        mShardCount = 0;
        mShards = nullptr;
        return;
    }

    mShardCount = normalized;
    mShards = static_cast<Shard*>(memory);

    for (usize i = 0; i < mShardCount; ++i)
    {
        new (&mShards[i]) Shard{};
    }
}

usize SmallObjectAllocator::SelectShardIndexForPointer(const void* ptr) const noexcept
{
    if (mShardCount == 1)
    {
        (void)ptr;
        return 0;
    }

    const auto address = static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ptr));
    const std::uintptr_t mixed = address >> 4;
    return static_cast<usize>((mixed * kPointerShardHashSalt) & ShardMask());
}

usize SmallObjectAllocator::SelectShardIndexForThread() const noexcept
{
    if (mShardCount == 1)
    {
        return 0;
    }

    const std::uint64_t fingerprint = ThreadFingerprint();
    return static_cast<usize>((fingerprint * kPointerShardHashSalt) & ShardMask());
}

SmallObjectAllocator::Shard& SmallObjectAllocator::ShardByIndex(usize index) const noexcept
{
    DNG_ASSERT(index < mShardCount, "SmallObjectAllocator shard index out of range");
    return mShards[index];
}

bool SmallObjectAllocator::AllocateSlabForClass(i32 classIdx, Class& klass) noexcept
{
    struct BatchList
    {
        FreeNode* Head{ nullptr };
        FreeNode* Tail{ nullptr };
        usize Count{ 0 };
    };

    std::scoped_lock slabGuard(klass.SlabMutex);

    u8* raw = static_cast<u8*>(mParent->Allocate(mCfg.SlabSizeBytes, alignof(std::max_align_t)));
    if (raw == nullptr)
    {
        HandleOutOfMemory(mCfg.SlabSizeBytes, alignof(std::max_align_t), "SmallObjectAllocator::AllocateSlab");
        return false;
    }

    const usize headerSize = AlignUp<usize>(sizeof(SlabHeader), 16);
    auto* slab = reinterpret_cast<SlabHeader*>(raw);
    slab->Next = klass.Slabs;
    slab->ClassIndex = classIdx;
    slab->Begin = raw + headerSize;
    slab->End = raw + mCfg.SlabSizeBytes;
    klass.Slabs = slab;

    const usize blockSize = EffectiveUserBlockSize(classIdx);
    const usize blockCount = (slab->End - slab->Begin) / blockSize;

    std::vector<BatchList> batches(mShardCount);
    usize totalEnqueued = 0;

    u8* cursor = slab->Begin;
    for (usize i = 0; i < blockCount; ++i)
    {
        auto* header = reinterpret_cast<BlockHeader*>(cursor);
        header->OwnerSlab = slab;
#if DNG_SMALLOBJ_TLS_BINS
        header->OwningThreadFingerprint = kNoThreadOwner;
#endif

        auto* node = reinterpret_cast<FreeNode*>(header + 1);
        node->Next = nullptr;

        const usize shardIndex = SelectShardIndexForPointer(node);
        BatchList& bucket = batches[shardIndex];
        if (bucket.Head == nullptr)
        {
            bucket.Head = bucket.Tail = node;
        }
        else
        {
            bucket.Tail->Next = node;
            bucket.Tail = node;
        }
        ++bucket.Count;
        ++totalEnqueued;

        cursor += blockSize;
    }

    for (usize shardIdx = 0; shardIdx < mShardCount; ++shardIdx)
    {
        BatchList& bucket = batches[shardIdx];
        if (bucket.Head == nullptr)
        {
            continue;
        }

        Shard& shard = ShardByIndex(shardIdx);
        std::unique_lock<std::mutex> shardLock(shard.Mutex);
        bucket.Tail->Next = shard.FreeLists[classIdx];
        shard.FreeLists[classIdx] = bucket.Head;
    }

    klass.FreeCount.fetch_add(totalEnqueued, std::memory_order_relaxed);
    klass.SlabCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

#if DNG_SMALLOBJ_TLS_BINS
SmallObjectAllocator::Magazine* SmallObjectAllocator::GetThreadMagazines() noexcept
{
    ThreadCache& cache = TLSBins::Cache();
    if (cache.OwnerInstance == this)
    {
        return cache.Magazines.data();
    }

    if (cache.OwnerInstance != nullptr && cache.OwnerInstance->IsAlive())
    {
        cache.OwnerInstance->FlushThreadCache(cache);
        cache.OwnerInstance = nullptr;
    }

    cache.Reset(mBaseBatch);
    cache.OwnerInstance = this;
    return cache.Magazines.data();
}

bool SmallObjectAllocator::RefillMagazine(i32 classIdx, Magazine& magazine) noexcept
{
    Class& klass = mClasses[classIdx];

    const auto now = Clock::now();
    if (magazine.HasRefilled)
    {
        const auto delta = now - magazine.LastRefillTime;
        const auto deltaNs = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
        if (deltaNs <= kFastRefillThresholdNs && magazine.Batch < kMagazineCapacity)
        {
            magazine.Batch = std::min(kMagazineCapacity, magazine.Batch * 2);
        }
        else if (deltaNs >= kIdleDecayThresholdNs && magazine.Batch > mBaseBatch)
        {
            magazine.Batch = std::max(mBaseBatch, magazine.Batch / 2);
        }
    }
    else
    {
        magazine.Batch = mBaseBatch;
    }
    magazine.LastRefillTime = now;
    magazine.HasRefilled = true;

    const usize desiredCount = std::min(kMagazineCapacity, magazine.Count + magazine.Batch);
    usize pulled = 0;

    while (magazine.Count < desiredCount)
    {
        const usize startShard = SelectShardIndexForThread();
        bool madeProgress = false;

        for (usize offset = 0; offset < mShardCount && magazine.Count < desiredCount; ++offset)
        {
            const usize shardIdx = (startShard + offset) & ShardMask();
            Shard& shard = ShardByIndex(shardIdx);
            std::unique_lock<std::mutex> shardLock(shard.Mutex);

            FreeNode*& freeList = shard.FreeLists[classIdx];
            while (freeList != nullptr && magazine.Count < desiredCount)
            {
                FreeNode* node = freeList;
                freeList = node->Next;

                node->Next = magazine.Head;
                magazine.Head = node;
                ++magazine.Count;
                ++pulled;
                madeProgress = true;
            }
        }

        if (madeProgress)
        {
            break;
        }

        if (!AllocateSlabForClass(classIdx, klass))
        {
            break;
        }
    }

    if (pulled > 0)
    {
        klass.FreeCount.fetch_sub(pulled, std::memory_order_relaxed);
        klass.CachedCount.fetch_add(pulled, std::memory_order_relaxed);
    }

    return magazine.Count != 0;
}

void SmallObjectAllocator::DrainMagazineToClass(i32 classIdx, Magazine& magazine, usize releaseCount) noexcept
{
    if (releaseCount == 0)
    {
        return;
    }

    if (magazine.Count == 0 || magazine.Head == nullptr)
    {
        magazine.Reset(mBaseBatch);
        return;
    }

    const usize toRelease = std::min(magazine.Count, releaseCount);
    if (toRelease == 0)
    {
        return;
    }

    struct BatchList
    {
        FreeNode* Head{ nullptr };
        FreeNode* Tail{ nullptr };
        usize Count{ 0 };
    };

    Class& klass = mClasses[classIdx];
    std::vector<BatchList> batches(mShardCount);

    FreeNode* current = magazine.Head;
    for (usize i = 0; i < toRelease; ++i)
    {
        DNG_CHECK(current != nullptr);
        auto* header = reinterpret_cast<BlockHeader*>(current) - 1;
        header->OwningThreadFingerprint = kNoThreadOwner;

        FreeNode* next = current->Next;
        current->Next = nullptr;

        const usize shardIdx = SelectShardIndexForPointer(current);
        BatchList& bucket = batches[shardIdx];
        if (bucket.Head == nullptr)
        {
            bucket.Head = bucket.Tail = current;
        }
        else
        {
            bucket.Tail->Next = current;
            bucket.Tail = current;
        }
        ++bucket.Count;

        current = next;
    }

    for (usize shardIdx = 0; shardIdx < mShardCount; ++shardIdx)
    {
        BatchList& bucket = batches[shardIdx];
        if (bucket.Head == nullptr)
        {
            continue;
        }

        Shard& shard = ShardByIndex(shardIdx);
        std::unique_lock<std::mutex> shardLock(shard.Mutex);
        bucket.Tail->Next = shard.FreeLists[classIdx];
        shard.FreeLists[classIdx] = bucket.Head;
    }

    klass.FreeCount.fetch_add(toRelease, std::memory_order_relaxed);
    const usize prevCache = klass.CachedCount.fetch_sub(toRelease, std::memory_order_relaxed);
    DNG_CHECK(prevCache >= toRelease);

    magazine.Head = current;
    magazine.Count -= toRelease;

    if (magazine.Count == 0 || magazine.Head == nullptr)
    {
        magazine.Reset(mBaseBatch);
    }
    else if (magazine.Batch > mBaseBatch)
    {
        magazine.Batch = std::max(mBaseBatch, magazine.Batch / 2);
    }
}

void SmallObjectAllocator::FlushThreadCache(ThreadCache& cache) noexcept
{
    if (cache.OwnerInstance != this)
    {
        return;
    }

    for (i32 i = 0; i < kNumClasses; ++i)
    {
        Magazine& magazine = cache.Magazines[static_cast<std::size_t>(i)];
        if (magazine.Count > 0)
        {
            DrainMagazineToClass(i, magazine, magazine.Count);
        }
    }

    cache.Reset(mBaseBatch);
    cache.OwnerInstance = nullptr;
}
#endif

bool SmallObjectAllocator::IsAlive() const noexcept
{
    return mAlive.load(std::memory_order_acquire);
}

void* SmallObjectAllocator::AllocateFromClass(ClassIndex ci, usize requestSize, usize alignment) noexcept
{
#if DNG_SMALLOBJ_TLS_BINS
    if (mTLSBinsEnabled)
    {
        DNG_ASSERT(ci.Index >= 0 && ci.Index < kNumClasses, "SmallObjectAllocator TLS class index out of range");
        DNG_ASSERT(requestSize > 0, "SmallObjectAllocator expects non-zero size in TLS path");
        const usize normalizedAlignment = NormalizeAlignment(alignment);
        DNG_ASSERT(normalizedAlignment == alignment, "TLS path requires normalized alignment");
        (void)normalizedAlignment;

        const usize tlsThreshold = static_cast<usize>(DNG_GLOBAL_NEW_SMALL_THRESHOLD);
        if (requestSize > tlsThreshold)
        {
            DNG_ASSERT(requestSize <= tlsThreshold && "TLS bins cannot service requests larger than the global small threshold");
            return AllocateFromShared(ci, requestSize, alignment);
        }

        const i32 classIdx = ci.Index;
        Magazine* magazines = GetThreadMagazines();
        Magazine& magazine = magazines[classIdx];
        Class& klass = mClasses[classIdx];

        if (magazine.Head != nullptr)
        {
            FreeNode* node = magazine.Head;
            magazine.Head = node->Next;
            --magazine.Count;
            const usize prevCache = klass.CachedCount.fetch_sub(1, std::memory_order_relaxed);
            DNG_CHECK(prevCache > 0);
            DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
            auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
            header->OwningThreadFingerprint = ThreadFingerprint();
            return static_cast<void*>(node);
        }

        if (!RefillMagazine(classIdx, magazine))
        {
            if (mCfg.ReturnNullOnOOM)
            {
                DNG_LOG_WARNING("Memory",
                    "SmallObjectAllocator OOM: class=%d request=%zu align=%zu",
                    static_cast<int>(classIdx),
                    static_cast<size_t>(requestSize),
                    static_cast<size_t>(alignment));
            }
            return nullptr;
        }

        FreeNode* node = magazine.Head;
        DNG_CHECK(node != nullptr);
        magazine.Head = node->Next;
        --magazine.Count;
        const usize prevCache = klass.CachedCount.fetch_sub(1, std::memory_order_relaxed);
        DNG_CHECK(prevCache > 0);
        DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
        auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
        header->OwningThreadFingerprint = ThreadFingerprint();
        return static_cast<void*>(node);
    }
#endif

    return AllocateFromShared(ci, requestSize, alignment);
}

void* SmallObjectAllocator::AllocateFromShared(ClassIndex ci, usize requestSize, usize alignment) noexcept
{
    (void)requestSize;
    const i32 classIdx = ci.Index;
    Class& klass = mClasses[classIdx];

    while (true)
    {
        const usize startShard = SelectShardIndexForThread();
        for (usize offset = 0; offset < mShardCount; ++offset)
        {
            const usize shardIdx = (startShard + offset) & ShardMask();
            Shard& shard = ShardByIndex(shardIdx);
            std::unique_lock<std::mutex> shardLock(shard.Mutex);

            FreeNode*& freeList = shard.FreeLists[classIdx];
            if (freeList == nullptr)
            {
                continue;
            }

            FreeNode* node = freeList;
            freeList = node->Next;
            shardLock.unlock();

            const usize prevFree = klass.FreeCount.fetch_sub(1, std::memory_order_relaxed);
            DNG_CHECK(prevFree > 0);

            DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
#if DNG_SMALLOBJ_TLS_BINS
            auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
            header->OwningThreadFingerprint = kNoThreadOwner;
#endif
            return static_cast<void*>(node);
        }

        if (!AllocateSlabForClass(classIdx, klass))
        {
            return nullptr;
        }
    }
}

void SmallObjectAllocator::ReturnToGlobal(FreeNode* node, i32 classIdx, std::uint64_t fingerprintHint) noexcept
{
#if DNG_SMALLOBJ_TLS_BINS
    auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
    header->OwningThreadFingerprint = kNoThreadOwner;
#endif
    (void)fingerprintHint;

    Class& klass = mClasses[classIdx];
    const usize shardIndex = SelectShardIndexForPointer(node);
    Shard& shard = ShardByIndex(shardIndex);
    std::unique_lock<std::mutex> shardLock(shard.Mutex);

    node->Next = shard.FreeLists[classIdx];
    shard.FreeLists[classIdx] = node;

    shardLock.unlock();
    klass.FreeCount.fetch_add(1, std::memory_order_relaxed);
}

void SmallObjectAllocator::FreeBlock(void* userPtr, i32 classIdx) noexcept
{
#if DNG_SMALLOBJ_TLS_BINS
    if (mTLSBinsEnabled)
    {
        auto* node = reinterpret_cast<FreeNode*>(userPtr);
        auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
        const std::uint64_t currentFingerprint = ThreadFingerprint();
        const std::uint64_t ownerFingerprint = header->OwningThreadFingerprint;

        if (ownerFingerprint != kNoThreadOwner && ownerFingerprint != currentFingerprint)
        {
            ReturnToGlobal(node, classIdx, ownerFingerprint);
            return;
        }

        header->OwningThreadFingerprint = currentFingerprint;

        Magazine* magazines = GetThreadMagazines();
        Magazine& magazine = magazines[classIdx];

        if (magazine.Count >= kMagazineCapacity)
        {
            const usize release = std::min(magazine.Count, std::max(magazine.Batch, mBaseBatch));
            DrainMagazineToClass(classIdx, magazine, release);
        }

        node->Next = magazine.Head;
        magazine.Head = node;
        ++magazine.Count;
        mClasses[classIdx].CachedCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }
#endif

    auto* node = reinterpret_cast<FreeNode*>(userPtr);
    ReturnToGlobal(node, classIdx, ThreadFingerprint());
}

void SmallObjectAllocator::HandleOutOfMemory(usize size, usize alignment, const char* context) const noexcept
{
    if (mCfg.ReturnNullOnOOM)
    {
        DNG_LOG_WARNING("Memory",
            "SmallObjectAllocator: allocation failure in {} (size={}, align={})",
            context ? context : "<unknown>",
            static_cast<size_t>(size),
            static_cast<size_t>(alignment));
    }
    else
    {
        DNG_MEM_CHECK_OOM(size, alignment, context);
    }
}

} // namespace dng::core
