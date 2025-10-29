#pragma once
// ============================================================================
// D-Engine - Core/Memory/SmallObjectAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a slab-backed allocator tuned for <= 1 KiB objects so hot
//           paths can avoid the general heap while still honouring the engine
//           allocator contract.
// Contract: All requests normalise alignment via `NormalizeAlignment`. Blocks
//           must be freed with the same `(size, alignment)`; larger requests or
//           unusual alignments fall back to the parent allocator. Per-class
//           mutexes deliver coarse thread-safety.
// Notes   : Slabs are sourced from a parent allocator. Diagnostics expose peak
//           usage via `DumpStats`. `Reallocate` is copy-based and never grows in
//           place.
// ============================================================================

#include "Core/Types.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Logger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>    // std::memcpy
#include <functional>
#include <mutex>
#include <new>        // std::nothrow
#include <thread>

namespace dng::core
{

// Purpose : Configuration knobs that tailor SmallObjectAllocator behaviour.
// Contract: Values are read-only after construction; caller owns the struct.
// Notes   : TLSBatchSize may be clamped by the allocator to [1, DNG_SOA_TLS_MAG_CAPACITY].
struct SmallObjectConfig
{
    usize SlabSizeBytes = 64 * 1024; // 64 KB per slab by default
    usize MaxClassSize = 1024;       // > MaxClassSize => route to Parent
    bool  ReturnNullOnOOM = false;   // if false => escalate to OOM policy
    // Allow callers to tune TLS refill batches without rebuilding; 0 defers to bench defaults.
    usize TLSBatchSize   = static_cast<usize>(DNG_SOALLOC_BATCH); // default TLS refill batch (bench derived)
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

            const usize minBatch = 1;
            const usize maxBatch = kMagazineCapacity;
            const usize requested = (cfg.TLSBatchSize == 0) ? kDefaultBatch : cfg.TLSBatchSize;
            mBaseBatch = std::clamp(requested, minBatch, maxBatch);
        }

        ~SmallObjectAllocator() override
        {
            ThreadCache& cache = sThreadCache;
            if (cache.Owner == this)
            {
                FlushThreadCache(cache);
                cache.Owner = nullptr;
                cache.Reset(mBaseBatch);
            }
            mAlive.store(false, std::memory_order_release);
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
            alignment = NormalizeAlignment(alignment);
            if (size == 0)
            {
                size = 1; // zero-byte allocs still consume a slot; keep contracts explicit
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

            alignment = NormalizeAlignment(alignment);
            if (size == 0)
            {
                size = 1;
            }

            if (size > mCfg.MaxClassSize)
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
        friend struct ThreadCache;

        // ---- Fixed size-class table (bytes) --------------------------------
        static constexpr std::array<usize, 7> kClassSizes = {
            16, 32, 64, 128, 256, 512, 1024
        };
        static constexpr i32   kNumClasses        = static_cast<i32>(kClassSizes.size());
        static constexpr usize kMagazineCapacity  = static_cast<usize>(DNG_SOA_TLS_MAG_CAPACITY);
        static constexpr usize kDefaultBatch      = static_cast<usize>(DNG_SOA_TLS_BATCH_COUNT);
        static constexpr usize kShardCount        = static_cast<usize>(DNG_SOA_SHARD_COUNT);

        static_assert(kMagazineCapacity >= 1,   "SmallObjectAllocator TLS magazine capacity must be >= 1");
        static_assert(kDefaultBatch   >= 1,     "SmallObjectAllocator TLS batch count must be >= 1");
        static_assert(kDefaultBatch   <= kMagazineCapacity, "TLS batch count cannot exceed magazine capacity");
        static_assert(kShardCount     >= 1,     "SmallObjectAllocator requires at least one shard");
        static_assert((kShardCount & (kShardCount - 1)) == 0, "Shard count must be a power of two");

        template<usize Count>
        struct ShardBitHelper
        {
            static constexpr unsigned value = 1 + ShardBitHelper<Count / 2>::value;
        };

        template<>
        struct ShardBitHelper<1>
        {
            static constexpr unsigned value = 0;
        };

        static constexpr unsigned kShardBits = ShardBitHelper<kShardCount>::value;
        static constexpr std::uint64_t kShardHashMultiplier = 11400714819323198485ull; // Knuth golden ratio
        static constexpr std::int64_t  kFastRefillThresholdNs = 200'000;   // 0.2 ms
        static constexpr std::int64_t  kIdleDecayThresholdNs  = 5'000'000; // 5 ms

        using Clock = std::chrono::steady_clock;

        struct ClassIndex { i32 Index = -1; };

        struct FreeNode;

        struct Shard
        {
            FreeNode* FreeList = nullptr;
            std::mutex Mutex;
        };

        struct Magazine
        {
            FreeNode* Head = nullptr;
            usize      Count = 0;
            usize      Batch = kDefaultBatch;
            Clock::time_point LastRefillTime{};
            bool       HasRefilled = false;

            void Reset(usize baseBatch) noexcept
            {
                Head = nullptr;
                Count = 0;
                Batch = baseBatch;
                LastRefillTime = {};
                HasRefilled = false;
            }
        };

        struct ThreadCache
        {
            SmallObjectAllocator* Owner = nullptr;
            Magazine Magazines[kNumClasses]{};

            ~ThreadCache() noexcept
            {
                if (Owner && Owner->IsAlive())
                {
                    Owner->FlushThreadCache(*this);
                }
                Owner = nullptr;
                Reset(SmallObjectAllocator::kDefaultBatch);
            }

            void Reset(usize baseBatch) noexcept
            {
                for (Magazine& mag : Magazines)
                {
                    mag.Reset(baseBatch);
                }
            }
        };

        static thread_local ThreadCache sThreadCache;

        [[nodiscard]] static std::uint64_t ThreadFingerprint() noexcept
        {
            thread_local const std::uint64_t value = []() noexcept {
                std::hash<std::thread::id> hasher;
                return static_cast<std::uint64_t>(hasher(std::this_thread::get_id()));
            }();
            return value;
        }

        [[nodiscard]] static usize SelectShard() noexcept
        {
            if constexpr (kShardCount == 1)
            {
                return 0;
            }
            const std::uint64_t hash = ThreadFingerprint() * kShardHashMultiplier;
            const unsigned shift = 64u - kShardBits;
            return static_cast<usize>(hash >> shift);
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

        // Internal per-block header to retrieve the owning slab on free().
        struct SlabHeader;
        struct BlockHeader
        {
            SlabHeader* OwnerSlab; // back-pointer to slab
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

        struct Class
        {
            SlabHeader* Slabs = nullptr;
            mutable std::mutex SlabMutex;
            std::array<Shard, kShardCount> Shards{};

            std::atomic<usize> SlabCount{ 0 };
            std::atomic<usize> FreeCount{ 0 };
            std::atomic<usize> CachedCount{ 0 };
        };

        IAllocator* mParent;
        SmallObjectConfig mCfg;
    usize            mBaseBatch{ kDefaultBatch };
        Class          mClasses[kNumClasses]{};
        std::atomic<bool> mAlive{ true };

        // --- Helpers ---

        ClassIndex SizeToClass(usize request) const noexcept
        {
            const i32 idx = ClassForSize(request);
            ClassIndex ci;
            ci.Index = idx;
            return ci;
        }

        // Size of the metadata placed before the user memory
        static constexpr usize BlockHeaderSize() noexcept { return sizeof(BlockHeader); }

        // Effective per-block payload size for a class (including header & padding to keep user aligned)
        usize EffectiveUserBlockSize(i32 classIdx) const noexcept
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

        usize BlocksPerSlab(i32 classIdx) const noexcept
        {
            const usize bsz = EffectiveUserBlockSize(classIdx);
            // Slab layout: [SlabHeader | padding-to-natural | blocks...]
            const usize headerArea = AlignUp<usize>(sizeof(SlabHeader), 16);
            if (mCfg.SlabSizeBytes <= headerArea + bsz) return 1;
            return (mCfg.SlabSizeBytes - headerArea) / bsz;
        }

        // ---
        // Purpose : Back new slab storage for the specified size-class and seed a shard.
        // Contract: Caller must hold the shard mutex; this function serialises slab creation via SlabMutex.
        // Notes   : On failure we honour ReturnNullOnOOM via HandleOutOfMemory().
        // ---
        bool AllocateSlabLocked(i32 ci, Class& klass, Shard& targetShard) noexcept
        {
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

            u8* cursor = slab->Begin;
            FreeNode* headNew = nullptr;
            for (usize i = 0; i < count; ++i)
            {
                auto* bh = reinterpret_cast<BlockHeader*>(cursor);
                bh->OwnerSlab = slab;

                auto* fn = reinterpret_cast<FreeNode*>(bh + 1);
                fn->Next = headNew;
                headNew = fn;

                cursor += blkSize;
            }

            if (headNew)
            {
                FreeNode* tail = headNew;
                while (tail->Next)
                {
                    tail = tail->Next;
                }
                tail->Next = targetShard.FreeList;
                targetShard.FreeList = headNew;
                klass.FreeCount.fetch_add(count, std::memory_order_relaxed);
            }

            klass.SlabCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        [[nodiscard]] Magazine* GetThreadMagazines() noexcept
        {
            ThreadCache& cache = sThreadCache;
            if (cache.Owner == this)
            {
                return cache.Magazines;
            }

            if (cache.Owner && cache.Owner->IsAlive())
            {
                cache.Owner->FlushThreadCache(cache);
                cache.Owner = nullptr;
            }

            cache.Reset(mBaseBatch);
            cache.Owner = this;
            return cache.Magazines;
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

            const usize shardIndex = SelectShard();
            Shard& shard = C.Shards[shardIndex];
            std::unique_lock<std::mutex> shardLock(shard.Mutex);

            usize pulled = 0;
            while (mag.Count < desiredCount)
            {
                if (!shard.FreeList)
                {
                    if (!AllocateSlabLocked(classIdx, C, shard))
                    {
                        break;
                    }
                    continue;
                }

                FreeNode* node = shard.FreeList;
                shard.FreeList = node->Next;
                C.FreeCount.fetch_sub(1, std::memory_order_relaxed);

                node->Next = mag.Head;
                mag.Head = node;
                ++mag.Count;
                ++pulled;
            }

            if (pulled > 0)
            {
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
            const usize shardIndex = SelectShard();
            Shard& shard = C.Shards[shardIndex];
            std::unique_lock<std::mutex> shardLock(shard.Mutex);

            FreeNode* batchHead = mag.Head;
            FreeNode* batchTail = batchHead;
            for (usize i = 1; i < toRelease; ++i)
            {
                DNG_CHECK(batchTail->Next != nullptr);
                batchTail = batchTail->Next;
            }

            FreeNode* remainingHead = batchTail->Next;
            batchTail->Next = shard.FreeList;
            shard.FreeList = batchHead;

            shardLock.unlock();

            C.FreeCount.fetch_add(toRelease, std::memory_order_relaxed);
            const usize prevCache = C.CachedCount.fetch_sub(toRelease, std::memory_order_relaxed);
            DNG_CHECK(prevCache >= toRelease);

            mag.Head = remainingHead;
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
            if (cache.Owner != this)
            {
                return;
            }
            for (i32 i = 0; i < kNumClasses; ++i)
            {
                Magazine& mag = cache.Magazines[i];
                if (mag.Count > 0)
                {
                    DrainMagazineToClass(i, mag, mag.Count);
                }
            }
            cache.Reset(mBaseBatch);
            cache.Owner = nullptr;
        }

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
            return static_cast<void*>(node);
        }

        // ---
        // Purpose : Return a block to the owning class, favouring TLS magazines.
        // Contract: Pointer must originate from AllocateFromClass for classIdx.
        // Notes   : Magazines drain to the shared free-list when full to limit drift.
        // ---
        void FreeBlock(void* userPtr, i32 classIdx) noexcept
        {
            auto* node = reinterpret_cast<FreeNode*>(userPtr);
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
                    "SmallObjectAllocator: allocation failure in %s (size=%zu, align=%zu)",
                    context ? context : "<unknown>", static_cast<size_t>(size), static_cast<size_t>(alignment));
            }
            else
            {
                DNG_MEM_CHECK_OOM(size, alignment, context);
            }
        }
    };

    inline thread_local SmallObjectAllocator::ThreadCache SmallObjectAllocator::sThreadCache{};

} // namespace dng::core
