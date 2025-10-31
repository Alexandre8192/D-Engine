#pragma once
// ============================================================================
// D-Engine - Core/Memory/TrackingAllocator.hpp
// ----------------------------------------------------------------------------
// Purpose : Wrap an `IAllocator` to expose leak diagnostics, per-tag snapshots,
//           and monotonic allocation counters used by diagnostics/benchmarks.
// Contract: Requires a non-null base allocator. All Allocate/Deallocate pairs
//           must respect the engine contract of matching `(size, alignment)`
//           after normalization. Thread-safety matches the wrapped allocator
//           except for optional leak maps guarded by an internal mutex.
// Notes   : Feature set is driven by compile-time toggles:
//           - `DNG_MEM_TRACKING` enables per-allocation maps and leak reports.
//           - `DNG_MEM_STATS_ONLY` keeps lightweight counters without maps.
//           - Monotonic counters stay active in every configuration.
// ============================================================================

#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Platform/PlatformMacros.hpp"
#include "Core/Types.hpp"

#include <array>
#include <atomic>
#if DNG_MEM_TRACKING
// Design Note: unordered_map + mutex remain behind DNG_MEM_TRACKING to keep stats-only builds lean.
#include <mutex>
#include <unordered_map>
#endif
#include <memory>
#include <cstring>   // std::memset
#include <cstddef>   // std::max_align_t
#include <cstdint>   // std::uint64_t

#ifndef DNG_MEM_TRACKING_ENABLED
#if defined(DNG_MEM_TRACKING)
#if DNG_MEM_TRACKING
#define DNG__TRACKING_FLAG 1
#else
#define DNG__TRACKING_FLAG 0
#endif
#else
#define DNG__TRACKING_FLAG 0
#endif
#if defined(DNG_MEM_STATS_ONLY)
#if DNG_MEM_STATS_ONLY
#define DNG__STATS_FLAG 1
#else
#define DNG__STATS_FLAG 0
#endif
#else
#define DNG__STATS_FLAG 0
#endif
#if DNG__TRACKING_FLAG || DNG__STATS_FLAG
#define DNG_MEM_TRACKING_ENABLED 1
#else
#define DNG_MEM_TRACKING_ENABLED 0
#endif
#undef DNG__TRACKING_FLAG
#undef DNG__STATS_FLAG
#endif
namespace dng::core {

    // Purpose : Enumerate allocation categories used for tagging, reporting, and leak analysis.
    // Contract: Values are stable for serialization; `Count` must remain last to size tag arrays safely.
    // Notes   : Extend with new tags as subsystems require; keep ordering deterministic for reports.
    enum class AllocTag : uint32 {
        General = 0,        ///< General purpose allocations
        Temporary,          ///< Short-lived temporary allocations
        Persistent,         ///< Long-lived persistent allocations
        Rendering,          ///< Graphics and rendering related
        Audio,              ///< Audio system allocations
        Networking,         ///< Network communication buffers
        Physics,            ///< Physics simulation data
        Scripting,          ///< Script engine allocations
        Assets,             ///< Asset loading and storage
        UI,                 ///< User interface elements

        // Add more categories as needed
        Count               ///< Total number of tags (keep last)
    };

    // Purpose : Carry allocation metadata (tag, name, optional callsite) alongside tracked blocks.
    // Contract: Trivially copyable; safe to pass by value; callsite fields populated only when enabled via macros.
    // Notes   : Defaults to `General` tag and "Unknown" name to keep instrumentation forward compatible.
    struct AllocInfo {
        AllocTag tag = AllocTag::General;           ///< Allocation category
        const char* name = "Unknown";               ///< Optional allocation name/description

#if DNG_MEM_CAPTURE_CALLSITE
        const char* file = nullptr;                ///< Source file (__FILE__)
        uint32 line = 0;                          ///< Source line (__LINE__)
#endif

        // Purpose : Create metadata with default tag/name when explicit parameters unavailable.
        // Contract: Leaves callsite fields untouched; constexpr-friendly for static usage.
        // Notes   : Allows aggregate initialization in constexpr contexts.
        constexpr AllocInfo() noexcept = default;

        // Purpose : Populate tag and human-readable name for diagnostics.
        // Contract: `n` must outlive tracked allocation; defaults to "Unknown" for optional usage.
        // Notes   : Callsite fields remain unset unless separate constructor used.
        constexpr AllocInfo(AllocTag t, const char* n = "Unknown") noexcept
            : tag(t), name(n) {
        }

#if DNG_MEM_CAPTURE_CALLSITE
        // Purpose : Capture tag, name, and source location for richer leak diagnostics.
        // Contract: `n` and `f` must remain valid strings; `l` carries one-based line number from __LINE__.
        // Notes   : Enabled only when callsite capture toggle is active.
        constexpr AllocInfo(AllocTag t, const char* n, const char* f, uint32 l) noexcept
            : tag(t), name(n), file(f), line(l) {
        }
#endif
    };

    // Purpose : Maintain live and historical statistics per allocation tag for diagnostics.
    // Contract: Thread-safe via atomic counters; callers read via relaxed loads unless stricter ordering required.
    // Notes   : Peak tracking is opportunistic (may under-report slightly under contention but never over-report).
    struct AllocatorStats {
        std::atomic<usize> current_bytes{ 0 };       ///< Current allocated bytes
        std::atomic<usize> peak_bytes{ 0 };          ///< Peak allocated bytes
        std::atomic<usize> total_allocations{ 0 };   ///< Total allocation count
        std::atomic<usize> current_allocations{ 0 }; ///< Current active allocations

        // Purpose : Clear all counters to zero, typically at frame/phase boundaries.
        // Contract: Atomic stores with relaxed ordering; safe under concurrent reads.
        // Notes   : Does not require external locks; use when you intentionally forget historical data.
        void Reset() noexcept {
            current_bytes.store(0);
            peak_bytes.store(0);
            total_allocations.store(0);
            current_allocations.store(0);
        }

        // Purpose : Accumulate allocation counters and update peak usage opportunistically.
        // Contract: Accepts the allocation `size` in bytes; relaxed atomics suffices for diagnostics.
        // Notes   : Peak update may retry under contention using compare_exchange_weak.
        void RecordAllocation(usize size) noexcept {
            current_bytes.fetch_add(size);
            total_allocations.fetch_add(1);
            current_allocations.fetch_add(1);

            // Update peak bytes (may race, but that's acceptable for statistics)
            usize current = current_bytes.load();
            usize peak = peak_bytes.load();
            while (current > peak && !peak_bytes.compare_exchange_weak(peak, current)) {
                // Retry if another thread updated peak_bytes
            }
        }

        // Purpose : Decrement live allocation counters when memory is freed.
        // Contract: `size` must equal the normalized allocation size tracked earlier when stats are enabled.
        // Notes   : Does not adjust `peak_bytes` because peak is monotonically non-decreasing.
        void RecordDeallocation(usize size) noexcept {
            current_bytes.fetch_sub(size);
            current_allocations.fetch_sub(1);
        }
    };

    // Purpose : Lightweight snapshot view consumed by leak diagnostics helpers.
    // Contract: Encapsulates the current allocation footprint (bytes and live allocations) per tag at capture time.
    // Notes   : Populated via relaxed atomic loads to keep the operation inexpensive under contention.
    struct TrackingSnapshotView
    {
        // Purpose : Aggregate allocation footprint for a single tag.
        // Contract: Plain data structure; members expressed in bytes and allocation counts.
        // Notes   : Default initialization yields a zeroed footprint.
        struct Tag
        {
            std::size_t bytes{ 0 };
            std::size_t allocs{ 0 };
        };

        std::array<Tag, static_cast<std::size_t>(AllocTag::Count)> byTag{};
        std::size_t totalBytes{ 0 };
        std::size_t totalAllocs{ 0 };
    };

    // Purpose : Immutable snapshot of ever-increasing counters since process start.
    // Contract: Values are monotonic totals; take differences between snapshots to derive windowed metrics.
    // Notes   : Independent from live stats, enabling cumulative diagnostics even when tracking is disabled.
    struct TrackingMonotonicCounters
    {
        std::uint64_t TotalAllocCalls     { 0 };
        std::uint64_t TotalFreeCalls      { 0 };
        std::uint64_t TotalBytesAllocated { 0 };
        std::uint64_t TotalBytesFreed     { 0 };
    };


#if DNG_MEM_TRACKING

    // Purpose : Persist detailed metadata for each live allocation when full tracking is enabled.
    // Contract: Stored only while `DNG_MEM_TRACKING` is active; size/alignment pairs remain normalized.
    // Notes   : Timestamp field currently unused but reserved for future temporal analysis.
    struct AllocationRecord {
        usize size;                    ///< Allocation size in bytes
        usize alignment;               ///< Allocation alignment
        AllocInfo info;                ///< Allocation metadata
        uint64 timestamp;              ///< Allocation timestamp (optional)

        // Purpose : Provide aggregate initialization for tracking maps.
        // Contract: Leaves fields zero-initialized; timestamp remains unspecified until set explicitly.
        // Notes   : Required for associative container value emplacement.
        AllocationRecord() = default;

        // Purpose : Capture allocation tuple details for leak diagnostics bookkeeping.
        // Contract: Parameters must already be normalized; `info` copied by value to retain metadata.
        // Notes   : Initializes timestamp to zero until a clock policy is adopted.
        AllocationRecord(usize s, usize a, const AllocInfo& i) noexcept
            : size(s), alignment(a), info(i), timestamp(0) {
        }
    };

#endif // DNG_MEM_TRACKING

    // Purpose : Wrap a base allocator to expose diagnostics, leak tracking, and monotonic allocation counters.
    // Contract: Requires a non-null `IAllocator`; (size, alignment) tuples must match the wrapped allocator's contract.
    // Notes   : Feature set depends on compile-time toggles (`DNG_MEM_TRACKING`, `DNG_MEM_STATS_ONLY`, `DNG_MEM_REPORT_ON_EXIT`).
    class TrackingAllocator : public IAllocator {
    private:
        IAllocator* m_baseAllocator;                    ///< Underlying allocator

#if DNG_MEM_TRACKING_ENABLED
        AllocatorStats m_stats[static_cast<usize>(AllocTag::Count)];  ///< Per-tag statistics
#endif

#if DNG_MEM_TRACKING
        struct AllocationShard
        {
            std::unordered_map<void*, AllocationRecord> allocations;
            mutable std::mutex mutex;
        };

    std::unique_ptr<AllocationShard[]> m_shards;    ///< Optional sharded allocation maps
    std::uint32_t m_shardCount{ 1 };                ///< Number of active shards (power-of-two)
    std::uint32_t m_shardMask{ 0 };                 ///< Mask for fast shard selection
    AllocationShard m_singleShard{};                ///< Storage when shardCount == 1

        void InitializeShards(std::uint32_t shardCount) noexcept;
        [[nodiscard]] AllocationShard& SelectShard(void* ptr) noexcept;
        [[nodiscard]] const AllocationShard& SelectShard(const void* ptr) const noexcept;
        [[nodiscard]] std::uint32_t ComputeShardIndex(const void* ptr) const noexcept;

        template <typename Fn>
        void VisitShards(Fn&& fn) noexcept
        {
            if (m_shardCount > 1u && m_shards)
            {
                for (std::uint32_t i = 0; i < m_shardCount; ++i)
                {
                    fn(m_shards[i]);
                }
            }
            else
            {
                fn(m_singleShard);
            }
        }

        template <typename Fn>
        void VisitShardsConst(Fn&& fn) const noexcept
        {
            if (m_shardCount > 1u && m_shards)
            {
                for (std::uint32_t i = 0; i < m_shardCount; ++i)
                {
                    const AllocationShard& shard = m_shards[i];
                    fn(shard);
                }
            }
            else
            {
                fn(m_singleShard);
            }
        }
#endif

    std::uint32_t m_samplingRate{ 1 };              ///< Track-every-N allocations (>=1)

    // Purpose : Cumulative totals for performance diagnostics.
    // Contract: 64-bit atomics; relaxed ordering is sufficient for totals.
    // Notes   : Counters stay valid even if leak tracking is disabled.
    //           BytesFreed may be 0 if size is unknown on Deallocate (non-tracking).
    std::atomic<std::uint64_t> m_totalAllocCalls     { 0 };
    std::atomic<std::uint64_t> m_totalFreeCalls      { 0 };
    std::atomic<std::uint64_t> m_totalBytesAllocated { 0 };
    std::atomic<std::uint64_t> m_totalBytesFreed     { 0 };


    public:
        // Purpose : Bind the tracking layer to an existing allocator implementation.
        // Contract: `baseAllocator` must be non-null and remain valid for the wrapper lifetime.
        // Notes   : Performs a defensive null check via `DNG_CHECK`.
        explicit TrackingAllocator(IAllocator* baseAllocator,
            std::uint32_t samplingRate = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SAMPLING_RATE),
            std::uint32_t shardCount   = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS)) noexcept
            : m_baseAllocator(baseAllocator)
            , m_samplingRate(samplingRate == 0u ? 1u : samplingRate)
        {
            DNG_CHECK(baseAllocator != nullptr);
#if DNG_MEM_TRACKING
            InitializeShards(shardCount);
#else
            (void)shardCount;
#endif
        }

        // Purpose : Optionally trigger leak reports on teardown based on compile-time policy flags.
        // Contract: No throwing; only active when both tracking and report-on-exit toggles are enabled.
        // Notes   : Keeps destructor lightweight when diagnostics are disabled.
        ~TrackingAllocator() noexcept override {
#if DNG_MEM_TRACKING && DNG_MEM_REPORT_ON_EXIT
            ReportLeaks();
#endif
        }

        // IAllocator interface implementation
        // Purpose : Fallback entry point for callers without explicit tag metadata.
        // Contract: Normalizes alignment, funnels into `AllocateTagged` with default info.
        // Notes   : Keeps legacy callers functional while still counting monotonic stats.
        [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            AllocInfo defaultInfo(AllocTag::General, "Untagged");
            return AllocateTagged(size, alignment, defaultInfo);
        }

        // Purpose : Return memory to the wrapped allocator while updating diagnostics as available.
        // Contract: Accepts optional size/alignment hints; must match the original tuple when provided.
        // Notes   : Queries allocation map when full tracking is enabled to recover canonical tuple.
        void Deallocate(void* ptr, usize size = 0, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (!ptr) return;

            alignment = NormalizeAlignment(alignment);
            usize forwardSize = size;
            usize forwardAlignment = alignment;

#if DNG_MEM_TRACKING
            // Full tracking mode: look up allocation record
            {
                AllocationShard& shard = SelectShard(ptr);
                std::lock_guard<std::mutex> lock(shard.mutex);
                auto it = shard.allocations.find(ptr);
                if (it != shard.allocations.end()) {
                    const AllocationRecord& record = it->second;
                    usize tagIndex = static_cast<usize>(record.info.tag);
                    if (tagIndex < static_cast<usize>(AllocTag::Count)) {
                        m_stats[tagIndex].RecordDeallocation(record.size);
                    }
                    forwardSize = record.size;
                    forwardAlignment = record.alignment;
                    shard.allocations.erase(it);
                }
            }
#elif DNG_MEM_STATS_ONLY
            // Statistics-only mode: use provided size hint
            if (size > 0) {
                m_stats[static_cast<usize>(AllocTag::General)].RecordDeallocation(size);
            }
#else
            DNG_UNUSED(size);
#endif
            // Monotonic counters (free path)
            m_totalFreeCalls.fetch_add(1, std::memory_order_relaxed);
            if (forwardSize > 0) {
                m_totalBytesFreed.fetch_add(static_cast<std::uint64_t>(forwardSize), std::memory_order_relaxed);
            }

            // Always forward the normalized (size, alignment) pair that matches the original allocation.
            m_baseAllocator->Deallocate(ptr, forwardSize, forwardAlignment);
        }


        // Purpose : Allocate memory while recording diagnostics metadata for the request.
        // Contract: Size must be non-zero; alignment normalized before delegating; invokes OOM policy on failure.
        // Notes   : Records into per-tag stats and optional allocation maps depending on build flags.
        [[nodiscard]] void* AllocateTagged(usize size, usize alignment, const AllocInfo& info) noexcept {
            if (size == 0) return nullptr;

            alignment = NormalizeAlignment(alignment);

            // Forward allocation to base allocator
            void* ptr = m_baseAllocator->Allocate(size, alignment);
            if (!ptr) {
                DNG_MEM_CHECK_OOM(size, alignment, "TrackingAllocator::AllocateTagged");
                return nullptr;
            }
            // Monotonic counters (allocation path)
            m_totalAllocCalls.fetch_add(1, std::memory_order_relaxed);
            m_totalBytesAllocated.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);

#if DNG_MEM_TRACKING
            // Full tracking mode: record allocation details
            {
                AllocationShard& shard = SelectShard(ptr);
                std::lock_guard<std::mutex> lock(shard.mutex);
                AllocationRecord record(size, alignment, info);
                shard.allocations[ptr] = record;
            }

            // Update statistics
            usize tagIndex = static_cast<usize>(info.tag);
            if (tagIndex < static_cast<usize>(AllocTag::Count)) {
                m_stats[tagIndex].RecordAllocation(size);
            }
#elif DNG_MEM_STATS_ONLY
            // Statistics-only mode: update counters
            usize tagIndex = static_cast<usize>(info.tag);
            if (tagIndex < static_cast<usize>(AllocTag::Count)) {
                m_stats[tagIndex].RecordAllocation(size);
            }
#else
            DNG_UNUSED(info);
#endif

            return ptr;
        }

#if DNG_MEM_TRACKING_ENABLED
        // Purpose : Expose per-tag live statistics collected for diagnostics.
        // Contract: `tag` must be within range; requires tracking or stats-only modes to be enabled.
        // Notes   : Returns a reference so callers can read atomic counters directly.
        [[nodiscard]] const AllocatorStats& GetStats(AllocTag tag) const noexcept {
            usize tagIndex = static_cast<usize>(tag);
            DNG_CHECK(tagIndex < static_cast<usize>(AllocTag::Count));
            return m_stats[tagIndex];
        }

        // Purpose : Clear accumulated statistics without touching active allocation records.
        // Contract: Safe to invoke while allocations are ongoing; per-tag counters reset to zero.
        // Notes   : Available only when per-tag statistics are compiled in.
        void ResetStats() noexcept {
            for (auto& stats : m_stats) {
                stats.Reset();
            }
        }
#endif // DNG_MEM_TRACKING_ENABLED

        // Purpose : Provide access to the wrapped allocator for advanced scenarios.
        // Contract: Pointer remains owned elsewhere; caller must not delete.
        // Notes   : Useful when callers need to forward requests without diagnostics.
        [[nodiscard]] IAllocator* GetBaseAllocator() const noexcept {
            return m_baseAllocator;
        }

        // Purpose : Capture an instantaneous per-tag aggregate suitable for leak snapshot comparisons.
        // Contract: Thread-safe; callable concurrently with Allocate/Deallocate; yields zeros when stats disabled.
        // Notes   : Relies solely on relaxed atomic loads to avoid locking overhead.
        [[nodiscard]] TrackingSnapshotView CaptureView() const noexcept
        {
            TrackingSnapshotView view{};

#if DNG_MEM_TRACKING_ENABLED
            for (std::size_t i = 0; i < view.byTag.size(); ++i)
            {
                const auto& stats = m_stats[i];
                const std::size_t bytes = stats.current_bytes.load(std::memory_order_relaxed);
                const std::size_t allocs = stats.current_allocations.load(std::memory_order_relaxed);
                view.byTag[i].bytes = bytes;
                view.byTag[i].allocs = allocs;
                view.totalBytes += bytes;
                view.totalAllocs += allocs;
            }
#endif

            return view;
        }

        // Purpose : Return a point-in-time copy of cumulative counters.
        // Contract: Lock-free; safe to invoke concurrently with allocations.
        // Notes   : Benchmarks consume the returned struct to compute deltas across iterations.
        [[nodiscard]] TrackingMonotonicCounters CaptureMonotonic() const noexcept
        {
            TrackingMonotonicCounters s;
            s.TotalAllocCalls     = m_totalAllocCalls.load(std::memory_order_relaxed);
            s.TotalFreeCalls      = m_totalFreeCalls.load(std::memory_order_relaxed);
            s.TotalBytesAllocated = m_totalBytesAllocated.load(std::memory_order_relaxed);
            s.TotalBytesFreed     = m_totalBytesFreed.load(std::memory_order_relaxed);
            return s;
        }

        // Forward declarations for methods implemented in task 7.2
        // Purpose : Emit aggregate statistics to the engine logger or diagnostics sinks.
        // Contract: No allocations; respects logger enablement checks before formatting output.
        // Notes   : Implemented in TrackingAllocator.cpp to keep header self-contained.
        void ReportStatistics() const noexcept;

#if DNG_MEM_TRACKING
        // Purpose : Produce a leak report enumerating unfreed allocations when tracking is active.
        // Contract: Requires `DNG_MEM_TRACKING`; acquires mutex while enumerating.
        // Notes   : Implemented out-of-line to keep compilation costs low.
        void ReportLeaks() const noexcept;

        // Purpose : Return the number of currently tracked active allocations.
        // Contract: Available only in full tracking mode; acquires the allocation map mutex.
        // Notes   : Primarily used by tests to assert leak-free behaviour.
        usize GetActiveAllocationCount() const noexcept;
#endif
    };

#if DNG_MEM_TRACKING
    inline void TrackingAllocator::InitializeShards(std::uint32_t shardCount) noexcept
    {
        if (shardCount == 0u || !::dng::core::IsPowerOfTwo(shardCount))
        {
            m_shardCount = 1u;
            m_shardMask = 0u;
            m_shards.reset();
            return;
        }

        m_shardCount = shardCount;
        m_shardMask = shardCount - 1u;
        if (m_shardCount > 1u)
        {
            m_shards = std::make_unique<AllocationShard[]>(m_shardCount);
        }
        else
        {
            m_shards.reset();
            m_shardMask = 0u;
        }
    }

    inline TrackingAllocator::AllocationShard& TrackingAllocator::SelectShard(void* ptr) noexcept
    {
        if (m_shardCount > 1u && m_shards)
        {
            const std::uint32_t index = ComputeShardIndex(ptr);
            return m_shards[index];
        }
        return m_singleShard;
    }

    inline const TrackingAllocator::AllocationShard& TrackingAllocator::SelectShard(const void* ptr) const noexcept
    {
        if (m_shardCount > 1u && m_shards)
        {
            const std::uint32_t index = ComputeShardIndex(ptr);
            return m_shards[index];
        }
        return m_singleShard;
    }

    inline std::uint32_t TrackingAllocator::ComputeShardIndex(const void* ptr) const noexcept
    {
        if (m_shardMask == 0u)
        {
            return 0u;
        }
        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ptr));
        return static_cast<std::uint32_t>((address >> 4u) & m_shardMask);
    }
#endif

#if DNG_MEM_TRACKING && DNG_MEM_REPORT_ON_EXIT
    // Purpose : Ensure ReportLeaks() is invoked on scope exit when diagnostics policy requires it.
    // Contract: Holds a non-owning pointer; leak report triggered only when pointer is non-null.
    // Notes   : Intended for stack-based helpers guarding allocator lifetimes.
    class ReportOnExit {
    private:
        TrackingAllocator* m_allocator;

    public:
        // Purpose : Bind to a TrackingAllocator for scoped leak emission.
        // Contract: `allocator` may be null; ownership remains external.
        // Notes   : constexpr not needed because helper used at runtime.
        explicit ReportOnExit(TrackingAllocator* allocator) noexcept
            : m_allocator(allocator) {
        }

        // Purpose : Invoke ReportLeaks on scope exit when a valid allocator is tracked.
        // Contract: Must remain noexcept; safe to call when allocator is null.
        // Notes   : Only meaningful when full tracking is compiled in.
        ~ReportOnExit() noexcept {
            if (m_allocator) {
                m_allocator->ReportLeaks();
            }
        }

        // Non-copyable, non-movable
        ReportOnExit(const ReportOnExit&) = delete;
        ReportOnExit& operator=(const ReportOnExit&) = delete;
        ReportOnExit(ReportOnExit&&) = delete;
        ReportOnExit& operator=(ReportOnExit&&) = delete;
    };
#endif

    // =============================
    // Convenience Macros
    // =============================

#if DNG_MEM_CAPTURE_CALLSITE
    // Purpose : Create AllocInfo with automatic callsite capture when enabled.
    // Contract: Expands to `AllocInfo` with tag, name, and optional file/line metadata.
    // Notes   : Keeps call sites compact while preserving deterministic diagnostics information.
#define DNG_ALLOC_INFO(tag, name) \
        dng::core::AllocInfo(tag, name, __FILE__, __LINE__)
#else
#define DNG_ALLOC_INFO(tag, name) \
        dng::core::AllocInfo(tag, name)
#endif

    // Purpose : Allocate through a TrackingAllocator while automatically constructing metadata.
    // Contract: `allocator` must be a pointer/reference to TrackingAllocator; forwards arguments verbatim.
    // Notes   : Avoids repetitive boilerplate at call sites where tagging is required.
#define DNG_ALLOC_TAGGED(allocator, size, alignment, tag, name) \
        (allocator)->AllocateTagged(size, alignment, DNG_ALLOC_INFO(tag, name))

} // namespace dng::core