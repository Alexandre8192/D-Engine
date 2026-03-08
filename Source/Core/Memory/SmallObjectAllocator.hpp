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

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

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
        explicit SmallObjectAllocator(IAllocator* parent, SmallObjectConfig cfg = {});

        ~SmallObjectAllocator() override;

        // ---
        // Purpose : Allow owning systems to flush per-thread caches when a thread terminates.
        // Contract: Safe to invoke even if TLS bins are disabled at compile time or runtime.
        // Notes   : Platform layers can hook this into their thread-detach callbacks when needed.
        void OnThreadExit() noexcept;

        // ---
        // Purpose : Allocate a small object while honouring the alignment contract.
        // Contract: Returns nullptr on OOM only if ReturnNullOnOOM==true or parent fails.
        //           The caller must pass the SAME (size, alignment) when freeing.
        // Notes   : Alignments > NaturalAlignFor(classSize) fall back to the parent allocator
        //           because current slab layout guarantees up to 16-byte alignment.
        // ---
        [[nodiscard]] void* Allocate(usize size, usize alignment) noexcept override;

        // ---
        // Purpose : Release a previously allocated small object back to its slab.
        // Contract: `(size, alignment)` must match the original request. Null pointers are ignored.
        // Notes   : Large blocks and high-alignment blocks are forwarded to the parent allocator.
        // ---
        void Deallocate(void* ptr, usize size, usize alignment) noexcept override;

        // ---
        // Purpose : Provide a conservative reallocate path (allocate-copy-free).
        // Contract: Mirrors IAllocator::Reallocate semantics, including nullptr/newSize==0 cases.
        // Notes   : Currently never performs in-place growth; wasInPlace is always false.
        // ---
        [[nodiscard]] void* Reallocate(void* ptr,
            usize oldSize,
            usize newSize,
            usize alignment = alignof(std::max_align_t),
            bool* wasInPlace = nullptr) noexcept override;

        // ---
        // Purpose : Emit per-class slab statistics for diagnostics.
        // Contract: Safe to call at any time; output is approximate due to relaxed atomics.
        // Notes   : Category defaults to "Memory" but callers may provide custom channels.
        // ---
        void DumpStats(const char* category = "Memory") const;

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

        [[nodiscard]] static std::uint64_t ThreadFingerprint() noexcept;

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

        void InitShards(usize requestedCount);

        [[nodiscard]] constexpr usize ShardMask() const noexcept
        {
            return mShardCount - 1;
        }

        [[nodiscard]] usize SelectShardIndexForPointer(const void* ptr) const noexcept;

        [[nodiscard]] usize SelectShardIndexForThread() const noexcept;

        [[nodiscard]] Shard& ShardByIndex(usize index) const noexcept;

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
        bool AllocateSlabForClass(i32 ci, Class& klass) noexcept;
#if DNG_SMALLOBJ_TLS_BINS
        [[nodiscard]] Magazine* GetThreadMagazines() noexcept;

        [[nodiscard]] bool RefillMagazine(i32 classIdx, Magazine& mag) noexcept;

        void DrainMagazineToClass(i32 classIdx, Magazine& mag, usize releaseCount) noexcept;

        void FlushThreadCache(ThreadCache& cache) noexcept;
#endif // DNG_SMALLOBJ_TLS_BINS

        [[nodiscard]] bool IsAlive() const noexcept;

        // ---
        // Purpose : Produce a block from the requested class via TLS magazines.
        // Contract: Caller ensures ci.Index is valid and alignment normalized.
        // Notes   : Slow path refills from the global free-list under mutex.
        // ---
        void* AllocateFromClass(ClassIndex ci, usize requestSize, usize alignment) noexcept;

        void* AllocateFromShared(ClassIndex ci, usize requestSize, usize alignment) noexcept;

    // ---
    // Purpose : Requeue a block into the shared shard selected by the fingerprint hint.
    // Contract: `node` must belong to `classIdx`; fingerprint hint may be zero to use the current thread.
    // Notes   : Runs in O(1) without logging to keep the slow path deterministic even under contention.
        void ReturnToGlobal(FreeNode* node, i32 classIdx, std::uint64_t fingerprintHint) noexcept;

        // ---
        // Purpose : Return a block to the owning class, favouring TLS magazines.
        // Contract: Pointer must originate from AllocateFromClass for classIdx.
        // Notes   : Magazines drain to the shared free-list when full to limit drift.
        // ---
        void FreeBlock(void* userPtr, i32 classIdx) noexcept;

        // ---
        // Purpose : Centralise OOM handling depending on configuration.
        // Contract: `context` must be a stable string literal for logging.
        // Notes   : In fatal mode this will not return (DNG_MEM_CHECK_OOM aborts).
        // ---
        void HandleOutOfMemory(usize size, usize alignment, const char* context) const noexcept;
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
