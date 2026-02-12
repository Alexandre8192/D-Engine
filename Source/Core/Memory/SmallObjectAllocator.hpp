#pragma once
// ============================================================================
// D-Engine - Core/Memory/SmallObjectAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a slab-backed allocator tuned for <= 1 KiB objects so hot
//           paths can avoid the general heap while still honouring the engine
//           allocator contract.
// Contract: All requests normalise alignment via `NormalizeAlignment`. Blocks
//           must be freed with the same `(size, alignment)`; larger requests or
//           unusual alignments fall back to the parent allocator. Sharded mutexes
//           reduce contention while keeping the design lock-based and auditable.
// Notes   : Slabs are sourced from a parent allocator. Diagnostics expose peak
//           usage via `DumpStats`. `Reallocate` is copy-based and never grows in
//           place. Thread-local magazines reduce contention and detect
//           cross-thread frees, forwarding them to sharded global lists keyed by
//           pointer hashes. Hot paths allocate exclusively through the configured
//           parent allocator with no hidden std:: allocations; callers must free
//           with the exact `(size, alignment)` pair supplied at allocation time.
// ============================================================================

#include "Core/Types.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemMacros.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/GlobalNewDelete.hpp"
#include "Core/Memory/OOM.hpp"
#if DNG_SMALLOBJ_TLS_BINS
#    include "Core/Memory/SmallObjectTLSBins.hpp"
#endif
#include "Core/Logger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>    // std::memcpy
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <memory>
#include <thread>
#include <vector>

namespace dng::core
{

// Purpose : Configuration knobs that tailor SmallObjectAllocator behaviour.
// Contract: Values are read-only after construction; caller owns the struct.
// Notes   : TLSBatchSize may be clamped by the allocator to [1, DNG_SOA_TLS_MAG_CAPACITY];
//           ShardCountOverride normalises to the nearest power-of-two when non-zero.
struct SmallObjectConfig
{
    usize SlabSizeBytes = 64 * 1024; // 64 KB per slab by default
    usize MaxClassSize = 1024;       // > MaxClassSize => route to Parent
    bool  ReturnNullOnOOM = false;   // if false => escalate to OOM policy
    // Allow callers to tune TLS refill batches without rebuilding; 0 defers to bench defaults.
    usize TLSBatchSize  = static_cast<usize>(DNG_SOALLOC_BATCH); // default TLS refill batch (bench derived)
    bool  EnableTLSBins = false;     // respected only when DNG_SMALLOBJ_TLS_BINS != 0.
    // Optional runtime override for shard fan-out; 0 => use compile-time default, value is normalized to power-of-two.
    usize ShardCountOverride = 0;
};

// ---
// Purpose : Provide a fast-path allocator for < 1 KiB payloads backed by slabs.
// Contract: Thread safety is per-class mutex (coarse) and depends on parent allocator
//           being thread-safe for slab procurement. Returned pointers obey
//           NormalizeAlignment within the guarantees documented in Notes.
// Notes   : Alignment requirements larger than the per-class natural alignment (16 bytes
//           today) are delegated to the parent allocator to keep slab layout compact.
//           Future revisions can specialise classes per alignment bucket if needed.
// ---
class SmallObjectAllocator final : public IAllocator
    {
    public:
        // ---
        // Purpose : Construct an allocator that sources slab memory from `parent`.
        // Contract: `parent` must outlive this instance and obey the IAllocator contract.
        // Notes   : We assert on entry because nullptr parent leaves the allocator unusable.
        // ---
        explicit SmallObjectAllocator(IAllocator* parent, SmallObjectConfig cfg = {})
            : mParent(parent), mCfg(cfg)
        {
            DNG_CHECK(mParent != nullptr);
            DNG_CHECK(mCfg.SlabSizeBytes >= 4096); // sanity guard against degenerate slabs

            if (mParent == nullptr)
            {
                mShardCount = 0;
                mShards = nullptr;
                return;
            }

            const usize minBatch = 1;
            const usize maxBatch = kMagazineCapacity;
            const usize requested = (cfg.TLSBatchSize == 0) ? kDefaultBatch : cfg.TLSBatchSize;
            mBaseBatch = std::clamp(requested, minBatch, maxBatch);

#if DNG_SMALLOBJ_TLS_BINS
            mTLSBinsEnabled = cfg.EnableTLSBins;
#endif
            const usize requestedShards = (cfg.ShardCountOverride == 0u)
                ? kDefaultShardCount
                : cfg.ShardCountOverride;
            InitShards(requestedShards);
            mCfg.ShardCountOverride = mShardCount;
        }

        ~SmallObjectAllocator() override
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

            // Clear shard free-list heads before slab teardown to avoid stale links.
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

            // Release all slabs acquired from the parent allocator.
            for (i32 classIdx = 0; classIdx < kNumClasses; ++classIdx)
            {
                Class& klass = mClasses[classIdx];
                std::scoped_lock slabGuard(klass.SlabMutex);

                SlabHeader* slab = klass.Slabs;
                while (slab != nullptr)
                {
                    SlabHeader* next = slab->Next;
                    mParent->Deallocate(static_cast<void*>(slab),
                        mCfg.SlabSizeBytes,
                        alignof(std::max_align_t));
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

        // ---
        // Purpose : Allow owning systems to flush per-thread caches when a thread terminates.
        // Contract: Safe to invoke even if TLS bins are disabled at compile time or runtime.
        // Notes   : Platform layers can hook this into their thread-detach callbacks when needed.
        void OnThreadExit() noexcept
        {
#if DNG_SMALLOBJ_TLS_BINS
            if (!mTLSBinsEnabled)
            {
                return;
            }
            TLSBins::FlushOnThreadExit(*this);
#endif
        }

        // ---
        // Purpose : Allocate a small object while honouring the alignment contract.
        // Contract: Returns nullptr on OOM only if ReturnNullOnOOM==true or parent fails.
        //           The caller must pass the SAME (size, alignment) when freeing.
        // Notes   : Alignments > NaturalAlignFor(classSize) fall back to the parent allocator
        //           because current slab layout guarantees up to 16-byte alignment.
        // ---
        [[nodiscard]] void* Allocate(usize size, usize alignment) noexcept override
        {
            if (mParent == nullptr)
            {
                return nullptr;
            }

            alignment = NormalizeAlignment(alignment);
            if (size == 0)
            {
                size = 1; // zero-byte allocs still consume a slot; keep contracts explicit
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

            const usize supportedAlignment = NormalizeAlignment(NaturalAlignFor(kClassSizes[(usize)ci.Index]));
            if (alignment > supportedAlignment)
            {
                // Current slab layout cannot satisfy this alignment without waste; delegate upstream.
                return mParent->Allocate(size, alignment);
            }

            return AllocateFromClass(ci, size, alignment);
        }

        // ---
        // Purpose : Release a previously allocated small object back to its slab.
        // Contract: `(size, alignment)` must match the original request. Null pointers are ignored.
        // Notes   : Large blocks and high-alignment blocks are forwarded to the parent allocator.
        // ---
        void Deallocate(void* ptr, usize size, usize alignment) noexcept override
        {
            if (!ptr)
            {
                return;
            }

            if (mParent == nullptr)
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
            auto* bh = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));
            SlabHeader* slab = bh->OwnerSlab;

            DNG_CHECK(slab != nullptr);
            const i32 classIdx = slab->ClassIndex;
            DNG_CHECK(classIdx >= 0 && classIdx < kNumClasses);

            FreeBlock(ptr, classIdx);
        }

        // ---
        // Purpose : Provide a conservative reallocate path (allocate-copy-free).
        // Contract: Mirrors IAllocator::Reallocate semantics, including nullptr/newSize==0 cases.
        // Notes   : Currently never performs in-place growth; wasInPlace is always false.
        // ---
        [[nodiscard]] void* Reallocate(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept override
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

            if (!ptr)
            {
                return Allocate(newSize, alignment);
            }

            if (oldSize == 0)
            {
                DNG_LOG_ERROR("Memory",
                    "SmallObjectAllocator::Reallocate misuse: ptr=%p oldSize==0 (alignment=%zu, newSize=%zu)",
                    ptr, static_cast<size_t>(alignment), static_cast<size_t>(newSize));
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
            if (!newBlock)
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

        // ---
        // Purpose : Emit per-class slab statistics for diagnostics.
        // Contract: Safe to call at any time; output is approximate due to relaxed atomics.
        // Notes   : Category defaults to "Memory" but callers may provide custom channels.
        // ---
        void DumpStats(const char* category = "Memory") const
        {
            usize totalSlabs = 0, totalMem = 0, totalFree = 0, totalBlocks = 0;

            for (i32 i = 0; i < kNumClasses; ++i)
            {
                const Class& c = mClasses[i];
                const usize blkSize = EffectiveUserBlockSize(i);
                const usize slabCount = c.SlabCount.load(std::memory_order_relaxed);
                const usize freeCount = c.FreeCount.load(std::memory_order_relaxed);
                const usize cachedCount = c.CachedCount.load(std::memory_order_relaxed);

                // Approx blocks = slabCount * blocksPerSlab(i)
                const usize bps = BlocksPerSlab(i);
                const usize blocks = slabCount * bps;

                totalSlabs += slabCount;
                totalBlocks += blocks;
                totalFree += (freeCount + cachedCount);
                totalMem += slabCount * mCfg.SlabSizeBytes;

                DNG_LOG_INFO(category,
                    "[SmallObject] class=%d size=%zu bytes, slabs=%zu, blocks=%zu, free=%zu (tls=%zu)",
                    (int)i, (size_t)blkSize, (size_t)slabCount, (size_t)blocks,
                    (size_t)freeCount, (size_t)cachedCount);
            }

            DNG_LOG_INFO(category,
                "[SmallObject] TOTAL slabs=%zu, blocks=%zu, free=%zu, mem=%zu KB",
                (size_t)totalSlabs, (size_t)totalBlocks, (size_t)totalFree, (size_t)(totalMem / 1024));
        }

    private:
        // ---- Fixed size-class table (bytes) --------------------------------
        static constexpr std::array<usize, 7> kClassSizes = {
            16, 32, 64, 128, 256, 512, 1024
        };
        static constexpr i32   kNumClasses        = static_cast<i32>(kClassSizes.size());
        static constexpr usize kMagazineCapacity  = static_cast<usize>(DNG_SOA_TLS_MAG_CAPACITY);
        static constexpr usize kDefaultBatch      = static_cast<usize>(DNG_SOA_TLS_BATCH_COUNT);
        static constexpr usize kDefaultShardCount = static_cast<usize>(DNG_SOA_SHARD_COUNT);

        static_assert(kMagazineCapacity >= 1,   "SmallObjectAllocator TLS magazine capacity must be >= 1");
        static_assert(kDefaultBatch   >= 1,     "SmallObjectAllocator TLS batch count must be >= 1");
        static_assert(kDefaultBatch   <= kMagazineCapacity, "TLS batch count cannot exceed magazine capacity");
        static_assert(kDefaultShardCount >= 1,  "SmallObjectAllocator requires at least one shard");

        static constexpr std::uint64_t kPointerShardHashSalt = 11400714819323198485ull; // Knuth golden ratio
        static constexpr std::int64_t  kFastRefillThresholdNs = 200'000;   // 0.2 ms
        static constexpr std::int64_t  kIdleDecayThresholdNs  = 5'000'000; // 5 ms

        using Clock = std::chrono::steady_clock;

        struct ClassIndex { i32 Index = -1; };

        struct FreeNode;

#if DNG_SMALLOBJ_TLS_BINS
        template<typename OwnerT, typename NodeT, std::size_t NumClassesT>
        friend class SmallObjectTLSBins;

        using TLSBins = SmallObjectTLSBins<SmallObjectAllocator, FreeNode, static_cast<std::size_t>(kNumClasses)>;
        using Magazine = typename TLSBins::Magazine;
        using ThreadCache = typename TLSBins::ThreadCache;
        static constexpr std::uint64_t kNoThreadOwner = TLSBins::kNoThreadOwner;
#endif

        [[nodiscard]] static std::uint64_t ThreadFingerprint() noexcept
        {
#if DNG_SMALLOBJ_TLS_BINS
            return TLSBins::ThreadFingerprint();
#else
            thread_local const std::uint64_t value = []() noexcept {
                std::hash<std::thread::id> hasher;
                std::uint64_t hashed = static_cast<std::uint64_t>(hasher(std::this_thread::get_id()));
                return hashed == 0ull ? 0x1ull : hashed;
            }();
            return value;
#endif
        }

        static constexpr i32 ClassForSize(usize s) noexcept
        {
            for (i32 i = 0; i < kNumClasses; ++i)
                if (s <= kClassSizes[(usize)i]) return i;
            return -1;
        }

        static constexpr usize NaturalAlignFor(usize s) noexcept
        {
            // Basic heuristic: align to min( max(8, nextPow2<=16), classSize )
            if (s <= 8)   return 8;
            if (s <= 16)  return 16;
            if (s <= 32)  return 16;
            if (s <= 64)  return 16;
            if (s <= 128) return 16;
            return 16; // MVP: keep it simple; alignment is capped by class block layout
        }

        [[nodiscard]] static constexpr usize NextPowerOfTwo(usize value) noexcept
        {
            if (value <= 1)
            {
                return 1;
            }

            --value;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
#if UINTPTR_MAX > 0xFFFFFFFFu
            value |= value >> 32;
#endif
            return value + 1;
        }

        [[nodiscard]] static constexpr usize NormalizeShardCount(usize requested) noexcept
        {
            if (requested == 0)
            {
                return 1;
            }
            return ::dng::core::IsPowerOfTwo(requested) ? requested : NextPowerOfTwo(requested);
        }

        // Internal per-block header to retrieve the owning slab on free().
        struct SlabHeader;
        struct BlockHeader
        {
            SlabHeader* OwnerSlab; // back-pointer to slab
#if DNG_SMALLOBJ_TLS_BINS
            std::uint64_t OwningThreadFingerprint = kNoThreadOwner;
#endif
        };

        // A slab contains contiguous blocks of the same class.
        struct SlabHeader
        {
            SlabHeader* Next = nullptr;
            i32         ClassIndex = -1;
            u8* Begin = nullptr; // first byte of user-block region
            u8* End = nullptr; // end of slab (exclusive)
        };

        // Free-list node stored at the beginning of the *user* region.
        struct FreeNode
        {
            FreeNode* Next;
        };

        struct Shard
        {
            std::mutex Mutex;
            FreeNode* FreeLists[kNumClasses]{};
        };

        struct Class
        {
            SlabHeader* Slabs = nullptr;
            mutable std::mutex SlabMutex;

            std::atomic<usize> SlabCount{ 0 };
            std::atomic<usize> FreeCount{ 0 };
            std::atomic<usize> CachedCount{ 0 };
        };

        void InitShards(usize requestedCount)
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

            void* mem = mParent->Allocate(bytes, alignment);
            if (mem == nullptr)
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
            mShards = static_cast<Shard*>(mem);

            for (usize i = 0; i < mShardCount; ++i)
            {
                new (&mShards[i]) Shard{};
            }
        }

        [[nodiscard]] constexpr usize ShardMask() const noexcept
        {
            return mShardCount - 1;
        }

        [[nodiscard]] usize SelectShardIndexForPointer(const void* ptr) const noexcept
        {
            if (mShardCount == 1)
            {
                (void)ptr;
                return 0;
            }

            const auto addr = static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ptr));
            const std::uintptr_t mixed = addr >> 4;
            const auto hashed = static_cast<usize>((mixed * kPointerShardHashSalt) & ShardMask());
            return hashed;
        }

        [[nodiscard]] usize SelectShardIndexForThread() const noexcept
        {
            if (mShardCount == 1)
            {
                return 0;
            }

            const std::uint64_t fingerprint = ThreadFingerprint();
            const auto hashed = static_cast<usize>((fingerprint * kPointerShardHashSalt) & ShardMask());
            return hashed;
        }

        [[nodiscard]] Shard& ShardByIndex(usize index) const noexcept
        {
            DNG_ASSERT(index < mShardCount, "SmallObjectAllocator shard index out of range");
            return mShards[index];
        }

        IAllocator*       mParent;
        SmallObjectConfig mCfg;
#if DNG_SMALLOBJ_TLS_BINS
        bool              mTLSBinsEnabled{ false };
#endif
        usize             mBaseBatch{ kDefaultBatch };
        usize             mShardCount{ 1 };
        Shard*            mShards{ nullptr };
        Class             mClasses[kNumClasses]{};
        std::atomic<bool> mAlive{ true };

        // --- Helpers ---

        [[nodiscard]] ClassIndex SizeToClass(usize request) const noexcept
        {
            const i32 idx = ClassForSize(request);
            ClassIndex ci;
            ci.Index = idx;
            return ci;
        }

        // Size of the metadata placed before the user memory
        [[nodiscard]] static constexpr usize BlockHeaderSize() noexcept { return sizeof(BlockHeader); }

        // Effective per-block payload size for a class (including header & padding to keep user aligned)
        [[nodiscard]] usize EffectiveUserBlockSize(i32 classIdx) const noexcept
        {
            const usize userMax = kClassSizes[(usize)classIdx];
            const usize natural = NaturalAlignFor(userMax);
            const usize hdr = BlockHeaderSize();

            // We must guarantee that returning (blockStart + sizeof(BlockHeader)) can be aligned to 'natural'
            // Simplify: we align the *start of block* to 'natural', then place header, then user.
            // Reserve max(header + alignment slack). MVP: pad block to next multiple of 'natural'.
            const usize minBlock = hdr + userMax;
            const usize padded = AlignUp<usize>(minBlock, natural);
            return padded;
        }

        [[nodiscard]] usize BlocksPerSlab(i32 classIdx) const noexcept
        {
            const usize bsz = EffectiveUserBlockSize(classIdx);
            // Slab layout: [SlabHeader | padding-to-natural | blocks...]
            const usize headerArea = AlignUp<usize>(sizeof(SlabHeader), 16);
            if (mCfg.SlabSizeBytes <= headerArea + bsz) return 1;
            return (mCfg.SlabSizeBytes - headerArea) / bsz;
        }

        // ---
        // Purpose : Back new slab storage for the specified size-class and fan-out blocks across shards.
        // Contract: Serialised via `Class::SlabMutex`. On failure the OOM policy is honoured.
        // Notes   : Blocks are hashed by user-pointer to pick their shard, ensuring deterministic routing.
        // ---
        bool AllocateSlabForClass(i32 ci, Class& klass) noexcept
        {
            struct BatchList
            {
                FreeNode* Head{ nullptr };
                FreeNode* Tail{ nullptr };
                usize Count{ 0 };
            };

            std::scoped_lock slabGuard(klass.SlabMutex);

            u8* raw = static_cast<u8*>(mParent->Allocate(mCfg.SlabSizeBytes, alignof(std::max_align_t)));
            if (!raw)
            {
                HandleOutOfMemory(mCfg.SlabSizeBytes, alignof(std::max_align_t), "SmallObjectAllocator::AllocateSlab");
                return false;
            }

            const usize hdrAlignedSize = AlignUp<usize>(sizeof(SlabHeader), 16);
            auto* slab = reinterpret_cast<SlabHeader*>(raw);
            slab->Next = klass.Slabs;
            slab->ClassIndex = ci;
            slab->Begin = raw + hdrAlignedSize;
            slab->End = raw + mCfg.SlabSizeBytes;
            klass.Slabs = slab;

            const usize blkSize = EffectiveUserBlockSize(ci);
            const usize count = (slab->End - slab->Begin) / blkSize;

            std::vector<BatchList> batches(mShardCount);
            usize totalEnqueued = 0;

            u8* cursor = slab->Begin;
            for (usize i = 0; i < count; ++i)
            {
                auto* bh = reinterpret_cast<BlockHeader*>(cursor);
                bh->OwnerSlab = slab;
#if DNG_SMALLOBJ_TLS_BINS
                bh->OwningThreadFingerprint = kNoThreadOwner;
#endif

                auto* fn = reinterpret_cast<FreeNode*>(bh + 1);
                fn->Next = nullptr;

                const usize shardIndex = SelectShardIndexForPointer(fn);
                BatchList& bucket = batches[shardIndex];
                if (!bucket.Head)
                {
                    bucket.Head = bucket.Tail = fn;
                }
                else
                {
                    bucket.Tail->Next = fn;
                    bucket.Tail = fn;
                }
                ++bucket.Count;
                ++totalEnqueued;

                cursor += blkSize;
            }

            for (usize shardIdx = 0; shardIdx < mShardCount; ++shardIdx)
            {
                BatchList& bucket = batches[shardIdx];
                if (!bucket.Head)
                {
                    continue;
                }

                Shard& shard = ShardByIndex(shardIdx);
                std::unique_lock<std::mutex> shardLock(shard.Mutex);
                bucket.Tail->Next = shard.FreeLists[ci];
                shard.FreeLists[ci] = bucket.Head;
            }

            klass.FreeCount.fetch_add(totalEnqueued, std::memory_order_relaxed);
            klass.SlabCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
#if DNG_SMALLOBJ_TLS_BINS
        [[nodiscard]] Magazine* GetThreadMagazines() noexcept
        {
            ThreadCache& cache = TLSBins::Cache();
            if (cache.OwnerInstance == this)
            {
                return cache.Magazines.data();
            }

            if (cache.OwnerInstance && cache.OwnerInstance->IsAlive())
            {
                cache.OwnerInstance->FlushThreadCache(cache);
                cache.OwnerInstance = nullptr;
            }

            cache.Reset(mBaseBatch);
            cache.OwnerInstance = this;
            return cache.Magazines.data();
        }

        [[nodiscard]] bool RefillMagazine(i32 classIdx, Magazine& mag) noexcept
        {
            Class& C = mClasses[classIdx];

            const auto now = Clock::now();
            if (mag.HasRefilled)
            {
                const auto delta = now - mag.LastRefillTime;
                const auto deltaNs = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
                if (deltaNs <= kFastRefillThresholdNs && mag.Batch < kMagazineCapacity)
                {
                    mag.Batch = std::min(kMagazineCapacity, mag.Batch * 2);
                }
                else if (deltaNs >= kIdleDecayThresholdNs && mag.Batch > mBaseBatch)
                {
                    mag.Batch = std::max(mBaseBatch, mag.Batch / 2);
                }
            }
            else
            {
                mag.Batch = mBaseBatch;
            }
            mag.LastRefillTime = now;
            mag.HasRefilled = true;

            const usize desiredCount = std::min(kMagazineCapacity, mag.Count + mag.Batch);

            usize pulled = 0;

            while (mag.Count < desiredCount)
            {
                const usize startShard = SelectShardIndexForThread();
                bool progress = false;

                for (usize offset = 0; offset < mShardCount && mag.Count < desiredCount; ++offset)
                {
                    const usize shardIdx = (startShard + offset) & ShardMask();
                    Shard& shard = ShardByIndex(shardIdx);
                    std::unique_lock<std::mutex> shardLock(shard.Mutex);

                    FreeNode*& freeList = shard.FreeLists[classIdx];
                    while (freeList && mag.Count < desiredCount)
                    {
                        FreeNode* node = freeList;
                        freeList = node->Next;

                        node->Next = mag.Head;
                        mag.Head = node;
                        ++mag.Count;
                        ++pulled;
                        progress = true;
                    }
                }

                if (progress)
                {
                    break;
                }

                if (!AllocateSlabForClass(classIdx, C))
                {
                    break;
                }
            }

            if (pulled > 0)
            {
                C.FreeCount.fetch_sub(pulled, std::memory_order_relaxed);
                C.CachedCount.fetch_add(pulled, std::memory_order_relaxed);
            }

            return mag.Count != 0;
        }

        void DrainMagazineToClass(i32 classIdx, Magazine& mag, usize releaseCount) noexcept
        {
            if (releaseCount == 0)
            {
                return;
            }

            if (mag.Count == 0 || !mag.Head)
            {
                mag.Reset(mBaseBatch);
                return;
            }

            const usize toRelease = std::min<usize>(mag.Count, releaseCount);
            if (toRelease == 0)
            {
                return;
            }

            Class& C = mClasses[classIdx];
            struct BatchList
            {
                FreeNode* Head{ nullptr };
                FreeNode* Tail{ nullptr };
                usize Count{ 0 };
            };

            std::vector<BatchList> batches(mShardCount);

            FreeNode* current = mag.Head;
            for (usize i = 0; i < toRelease; ++i)
            {
                DNG_CHECK(current != nullptr);
                auto* header = reinterpret_cast<BlockHeader*>(current) - 1;
                header->OwningThreadFingerprint = kNoThreadOwner;

                FreeNode* next = current->Next;
                current->Next = nullptr;

                const usize shardIdx = SelectShardIndexForPointer(current);
                BatchList& bucket = batches[shardIdx];
                if (!bucket.Head)
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
                if (!bucket.Head)
                {
                    continue;
                }

                Shard& shard = ShardByIndex(shardIdx);
                std::unique_lock<std::mutex> shardLock(shard.Mutex);
                bucket.Tail->Next = shard.FreeLists[classIdx];
                shard.FreeLists[classIdx] = bucket.Head;
            }

            C.FreeCount.fetch_add(toRelease, std::memory_order_relaxed);
            const usize prevCache = C.CachedCount.fetch_sub(toRelease, std::memory_order_relaxed);
            DNG_CHECK(prevCache >= toRelease);

            mag.Head = current;
            mag.Count -= toRelease;

            if (mag.Count == 0 || !mag.Head)
            {
                mag.Reset(mBaseBatch);
            }
            else if (mag.Batch > mBaseBatch)
            {
                mag.Batch = std::max(mBaseBatch, mag.Batch / 2);
            }
        }

        void FlushThreadCache(ThreadCache& cache) noexcept
        {
            if (cache.OwnerInstance != this)
            {
                return;
            }
            for (i32 i = 0; i < kNumClasses; ++i)
            {
                const std::size_t idx = static_cast<std::size_t>(i);
                Magazine& mag = cache.Magazines[idx];
                if (mag.Count > 0)
                {
                    DrainMagazineToClass(i, mag, mag.Count);
                }
            }
            cache.Reset(mBaseBatch);
            cache.OwnerInstance = nullptr;
        }
#endif // DNG_SMALLOBJ_TLS_BINS

        [[nodiscard]] bool IsAlive() const noexcept
        {
            return mAlive.load(std::memory_order_acquire);
        }

        // ---
        // Purpose : Produce a block from the requested class via TLS magazines.
        // Contract: Caller ensures ci.Index is valid and alignment normalized.
        // Notes   : Slow path refills from the global free-list under mutex.
        // ---
        void* AllocateFromClass(ClassIndex ci, usize requestSize, usize alignment) noexcept
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

                const i32 idx = ci.Index;
                Magazine* magazines = GetThreadMagazines();
                Magazine& mag = magazines[idx];
                Class& C = mClasses[idx];

                if (mag.Head)
                {
                    FreeNode* node = mag.Head;
                    mag.Head = node->Next;
                    --mag.Count;
                    const usize prevCache = C.CachedCount.fetch_sub(1, std::memory_order_relaxed);
                    DNG_CHECK(prevCache > 0);
                    DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
                    auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
                    header->OwningThreadFingerprint = ThreadFingerprint();
                    return static_cast<void*>(node);
                }

                if (!RefillMagazine(idx, mag))
                {
                    if (mCfg.ReturnNullOnOOM)
                    {
                        DNG_LOG_WARNING("Memory",
                            "SmallObjectAllocator OOM: class=%d request=%zu align=%zu",
                            (int)idx, static_cast<size_t>(requestSize), static_cast<size_t>(alignment));
                    }
                    return nullptr;
                }

                FreeNode* node = mag.Head;
                DNG_CHECK(node != nullptr);
                mag.Head = node->Next;
                --mag.Count;
                const usize prevCache = C.CachedCount.fetch_sub(1, std::memory_order_relaxed);
                DNG_CHECK(prevCache > 0);
                DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
                auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
                header->OwningThreadFingerprint = ThreadFingerprint();
                return static_cast<void*>(node);
            }
#endif
            return AllocateFromShared(ci, requestSize, alignment);
        }

        void* AllocateFromShared(ClassIndex ci, usize requestSize, usize alignment) noexcept
        {
            (void)requestSize;
            const i32 idx = ci.Index;
            Class& C = mClasses[idx];

            while (true)
            {
                const usize startShard = SelectShardIndexForThread();
                for (usize offset = 0; offset < mShardCount; ++offset)
                {
                    const usize shardIdx = (startShard + offset) & ShardMask();
                    Shard& shard = ShardByIndex(shardIdx);
                    std::unique_lock<std::mutex> shardLock(shard.Mutex);

                    FreeNode*& freeList = shard.FreeLists[idx];
                    if (!freeList)
                    {
                        continue;
                    }

                    FreeNode* node = freeList;
                    freeList = node->Next;
                    shardLock.unlock();

                    const usize prevFree = C.FreeCount.fetch_sub(1, std::memory_order_relaxed);
                    DNG_CHECK(prevFree > 0);

                    DNG_ASSERT(IsAligned(node, alignment), "SmallObjectAllocator returned misaligned pointer");
#if DNG_SMALLOBJ_TLS_BINS
                    auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
                    header->OwningThreadFingerprint = kNoThreadOwner;
#endif
                    return static_cast<void*>(node);
                }

                if (!AllocateSlabForClass(idx, C))
                {
                    return nullptr;
                }
            }
        }

    // ---
    // Purpose : Requeue a block into the shared shard selected by the fingerprint hint.
    // Contract: `node` must belong to `classIdx`; fingerprint hint may be zero to use the current thread.
    // Notes   : Runs in O(1) without logging to keep the slow path deterministic even under contention.
    void ReturnToGlobal(FreeNode* node, i32 classIdx, std::uint64_t fingerprintHint) noexcept
        {
#if DNG_SMALLOBJ_TLS_BINS
            auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
            header->OwningThreadFingerprint = kNoThreadOwner;
#endif
            (void)fingerprintHint;
            Class& C = mClasses[classIdx];
            const usize shardIndex = SelectShardIndexForPointer(node);
            Shard& shard = ShardByIndex(shardIndex);
            std::unique_lock<std::mutex> shardLock(shard.Mutex);

            node->Next = shard.FreeLists[classIdx];
            shard.FreeLists[classIdx] = node;

            shardLock.unlock();

            C.FreeCount.fetch_add(1, std::memory_order_relaxed);
        }

        // ---
        // Purpose : Return a block to the owning class, favouring TLS magazines.
        // Contract: Pointer must originate from AllocateFromClass for classIdx.
        // Notes   : Magazines drain to the shared free-list when full to limit drift.
        // ---
        void FreeBlock(void* userPtr, i32 classIdx) noexcept
        {
#if DNG_SMALLOBJ_TLS_BINS
            if (mTLSBinsEnabled)
            {
                auto* node = reinterpret_cast<FreeNode*>(userPtr);
                auto* header = reinterpret_cast<BlockHeader*>(node) - 1;
                const std::uint64_t currentFingerprint = ThreadFingerprint();
                const std::uint64_t ownerFingerprint = header->OwningThreadFingerprint;

                // Purpose : Detect cross-thread frees and return blocks via shared shards deterministically.
                // Contract: When fingerprints differ, no TLS mutation occurs; the block goes straight to the shard list in O(1).
                // Notes   : Keeps TLS magazines strictly single-owner without logging or OS involvement on the hot path.
                if (ownerFingerprint != kNoThreadOwner && ownerFingerprint != currentFingerprint)
                {
                    ReturnToGlobal(node, classIdx, ownerFingerprint);
                    return;
                }

                header->OwningThreadFingerprint = currentFingerprint;

                Magazine* magazines = GetThreadMagazines();
                Magazine& mag = magazines[classIdx];

                if (mag.Count >= kMagazineCapacity)
                {
                    const usize release = std::min(mag.Count, std::max(mag.Batch, mBaseBatch));
                    DrainMagazineToClass(classIdx, mag, release);
                }

                node->Next = mag.Head;
                mag.Head = node;
                ++mag.Count;
                mClasses[classIdx].CachedCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }
#endif
            auto* node = reinterpret_cast<FreeNode*>(userPtr);
            ReturnToGlobal(node, classIdx, ThreadFingerprint());
        }

        // ---
        // Purpose : Centralise OOM handling depending on configuration.
        // Contract: `context` must be a stable string literal for logging.
        // Notes   : In fatal mode this will not return (DNG_MEM_CHECK_OOM aborts).
        // ---
        void HandleOutOfMemory(usize size, usize alignment, const char* context) const noexcept
        {
            if (mCfg.ReturnNullOnOOM)
            {
                DNG_LOG_WARNING("Memory",
                    "SmallObjectAllocator: allocation failure in {} (size={}, align={})",
                    context ? context : "<unknown>", static_cast<size_t>(size), static_cast<size_t>(alignment));
            }
            else
            {
                DNG_MEM_CHECK_OOM(size, alignment, context);
            }
        }
#if !DNG_SMALLOBJ_TLS_BINS
        struct SmallObjectAllocatorNoTLSLayout : IAllocator
        {
            IAllocator*       Parent;
            SmallObjectConfig Config;
            usize             BaseBatch;
            usize             ShardCount;
            Shard*            Shards;
            Class             Classes[kNumClasses];
            std::atomic<bool> Alive;
        };

    public:
        // Purpose : Snapshot the allocator footprint when TLS bins are compiled out.
        // Contract: constexpr audit hooks; do not persist or depend on at runtime.
        // Notes   : Exists solely to back the compile-time static_asserts below; no hidden work.
        static constexpr std::size_t kNoTLSSize = sizeof(SmallObjectAllocatorNoTLSLayout);
        static constexpr std::size_t kNoTLSAlignment = alignof(SmallObjectAllocatorNoTLSLayout);
    private:
#endif
    };

} // namespace dng::core

#if !DNG_SMALLOBJ_TLS_BINS
static_assert(::dng::core::SmallObjectAllocator::kNoTLSSize == sizeof(::dng::core::SmallObjectAllocator),
    "SmallObjectAllocator ABI changed when TLS bins disabled.");
static_assert(::dng::core::SmallObjectAllocator::kNoTLSAlignment == alignof(::dng::core::SmallObjectAllocator),
    "SmallObjectAllocator alignment changed when TLS bins disabled.");
#endif
