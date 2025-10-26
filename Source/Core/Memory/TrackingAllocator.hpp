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

#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Types.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
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




// Fallbacks for minimal, side-effect-free build (only if engine macros are not available)
#ifndef DNG_CHECK
#define DNG_CHECK(expr) do { (void)sizeof((expr)); } while(0)
#endif
#ifndef DNG_UNUSED
#define DNG_UNUSED(x) (void)(x)
#endif
namespace dng::core {

    /**
     * @brief Allocation categorization tags for tracking and profiling
     *
     * These tags allow developers to categorize allocations for better
     * memory usage analysis and leak detection.
     */
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

    /**
     * @brief Allocation information for tracking and debugging
     *
     * Contains metadata about an allocation including categorization
     * and optional callsite information for leak detection.
     */
    struct AllocInfo {
        AllocTag tag = AllocTag::General;           ///< Allocation category
        const char* name = "Unknown";               ///< Optional allocation name/description

#if DNG_MEM_CAPTURE_CALLSITE
        const char* file = nullptr;                ///< Source file (__FILE__)
        uint32 line = 0;                          ///< Source line (__LINE__)
#endif

        /// Default constructor
        constexpr AllocInfo() noexcept = default;

        /// Constructor with tag and name
        constexpr AllocInfo(AllocTag t, const char* n = "Unknown") noexcept
            : tag(t), name(n) {
        }

#if DNG_MEM_CAPTURE_CALLSITE
        /// Constructor with callsite information
        constexpr AllocInfo(AllocTag t, const char* n, const char* f, uint32 l) noexcept
            : tag(t), name(n), file(f), line(l) {
        }
#endif
    };

    /**
     * @brief Per-tag allocation statistics
     *
     * Tracks current usage, peak usage, and allocation counts
     * for each allocation tag category.
     */
    struct AllocatorStats {
        std::atomic<usize> current_bytes{ 0 };       ///< Current allocated bytes
        std::atomic<usize> peak_bytes{ 0 };          ///< Peak allocated bytes
        std::atomic<usize> total_allocations{ 0 };   ///< Total allocation count
        std::atomic<usize> current_allocations{ 0 }; ///< Current active allocations

        /// Reset all statistics to zero
        void Reset() noexcept {
            current_bytes.store(0);
            peak_bytes.store(0);
            total_allocations.store(0);
            current_allocations.store(0);
        }

        /// Update statistics for a new allocation
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

        /// Update statistics for a deallocation
        void RecordDeallocation(usize size) noexcept {
            current_bytes.fetch_sub(size);
            current_allocations.fetch_sub(1);
        }
    };

    // ---
    // Purpose : Lightweight snapshot view consumed by leak diagnostics helpers.
    // Contract: Encapsulates the CURRENT allocation footprint (bytes and active
    //           allocation count) per AllocTag at capture time.
    // Notes   : Populated by TrackingAllocator::CaptureView using relaxed atomic
    //           loads so that snapshotting remains a low-overhead operation even
    //           when the allocator is under contention.
    // ---
    struct TrackingSnapshotView
    {
        struct Tag
        {
            std::size_t bytes{ 0 };
            std::size_t allocs{ 0 };
        };

        std::array<Tag, static_cast<std::size_t>(AllocTag::Count)> byTag{};
        std::size_t totalBytes{ 0 };
        std::size_t totalAllocs{ 0 };
    };

    // ---
    // Purpose : Immutable snapshot of ever-increasing counters since process start.
    // Contract: All fields are totals (monotonic). Use (after - before) to compute
    //           a delta over any time window. Thread-safe to read.
    // Notes   : Independent from "live" stats used for leak detection.
    // ---
    struct TrackingMonotonicCounters
    {
        std::uint64_t TotalAllocCalls     { 0 };
        std::uint64_t TotalFreeCalls      { 0 };
        std::uint64_t TotalBytesAllocated { 0 };
        std::uint64_t TotalBytesFreed     { 0 };
    };


#if DNG_MEM_TRACKING

    /**
     * @brief Full allocation tracking record
     *
     * Used in full tracking mode to store detailed information
     * about each individual allocation for leak detection.
     */
    struct AllocationRecord {
        usize size;                    ///< Allocation size in bytes
        usize alignment;               ///< Allocation alignment
        AllocInfo info;                ///< Allocation metadata
        uint64 timestamp;              ///< Allocation timestamp (optional)

        AllocationRecord() = default;
        AllocationRecord(usize s, usize a, const AllocInfo& i) noexcept
            : size(s), alignment(a), info(i), timestamp(0) {
        }
    };

#endif // DNG_MEM_TRACKING

    /**
     * @brief Diagnostic wrapper allocator with tracking and leak detection
     *
     * TrackingAllocator wraps any IAllocator implementation to provide
     * allocation tracking, statistics, and leak detection capabilities.
     *
     * The allocator supports two tracking modes controlled by compile-time macros:
     *
     * 1. Full Tracking (DNG_MEM_TRACKING=1):
     *    - Maintains hash table of all allocations
     *    - Provides detailed leak reports with callsite information
     *    - Tracks per-tag statistics and peak usage
     *    - Significant memory and performance overhead
     *
     * 2. Statistics Only (DNG_MEM_STATS_ONLY=1):
     *    - Maintains global counters only
     *    - No per-allocation tracking or leak detection
     *    - Minimal overhead suitable for production profiling
     *
     * 3. Disabled (both macros = 0):
     *    - Compiles to minimal overhead wrapper
     *    - No tracking or statistics
     *    - Suitable for release builds
     *
     * Usage Examples:
     * @code
     * // Wrap any allocator with tracking
     * DefaultAllocator baseAllocator;
     * TrackingAllocator tracker(&baseAllocator);
     * AllocatorRef allocRef(&tracker);
     *
     * // Allocate with tag information
     * AllocInfo info(AllocTag::Rendering, "Vertex Buffer");
     * void* buffer = tracker.AllocateTagged(1024, 16, info);
     * tracker.Deallocate(buffer, 1024, 16);
     *
     * // Generate statistics report
     * tracker.ReportStatistics();
     * @endcode
     */
    class TrackingAllocator : public IAllocator {
    private:
        IAllocator* m_baseAllocator;                    ///< Underlying allocator

#if DNG_MEM_TRACKING_ENABLED
        AllocatorStats m_stats[static_cast<usize>(AllocTag::Count)];  ///< Per-tag statistics
#endif

#if DNG_MEM_TRACKING
        std::unordered_map<void*, AllocationRecord> m_allocations;    ///< Active allocations map
        mutable std::mutex m_mutex;                     ///< Thread safety for allocation map
#endif

    // --- Monotonic churn counters (ever growing) -------------------------------
    // Purpose : Cumulative totals for performance diagnostics.
    // Contract: 64-bit atomics; relaxed ordering is sufficient for totals.
    // Notes   : Counters stay valid even if leak tracking is disabled.
    //           BytesFreed may be 0 if size is unknown on Deallocate (non-tracking).
    std::atomic<std::uint64_t> m_totalAllocCalls     { 0 };
    std::atomic<std::uint64_t> m_totalFreeCalls      { 0 };
    std::atomic<std::uint64_t> m_totalBytesAllocated { 0 };
    std::atomic<std::uint64_t> m_totalBytesFreed     { 0 };


    public:
        /**
         * @brief Constructor wrapping a base allocator
         *
         * @param baseAllocator Underlying allocator to wrap (must not be null)
         */
        explicit TrackingAllocator(IAllocator* baseAllocator) noexcept
            : m_baseAllocator(baseAllocator) {
            DNG_CHECK(baseAllocator != nullptr);
        }

        /**
         * @brief Destructor with optional leak reporting
         *
         * If DNG_MEM_REPORT_ON_EXIT is enabled and DNG_MEM_TRACKING is enabled,
         * automatically reports any memory leaks on destruction.
         */
        ~TrackingAllocator() noexcept override {
#if DNG_MEM_TRACKING && DNG_MEM_REPORT_ON_EXIT
            ReportLeaks();
#endif
        }

        // IAllocator interface implementation
        [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept override {
            AllocInfo defaultInfo(AllocTag::General, "Untagged");
            return AllocateTagged(size, alignment, defaultInfo);
        }

        void Deallocate(void* ptr, usize size = 0, usize alignment = alignof(std::max_align_t)) noexcept override {
            if (!ptr) return;

            alignment = NormalizeAlignment(alignment);
            usize forwardSize = size;
            usize forwardAlignment = alignment;

#if DNG_MEM_TRACKING
            // Full tracking mode: look up allocation record
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_allocations.find(ptr);
                if (it != m_allocations.end()) {
                    const AllocationRecord& record = it->second;
                    usize tagIndex = static_cast<usize>(record.info.tag);
                    if (tagIndex < static_cast<usize>(AllocTag::Count)) {
                        m_stats[tagIndex].RecordDeallocation(record.size);
                    }
                    forwardSize = record.size;
                    forwardAlignment = record.alignment;
                    m_allocations.erase(it);
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


        /**
         * @brief Allocate memory with tag information
         *
         * @param size Size in bytes to allocate
         * @param alignment Alignment requirement
         * @param info Allocation metadata for tracking
         * @return Allocated memory pointer or nullptr on failure
         */
        [[nodiscard]] void* AllocateTagged(usize size, usize alignment, const AllocInfo& info) noexcept {
            if (size == 0) return nullptr;

            alignment = NormalizeAlignment(alignment);

            // Forward allocation to base allocator
            void* ptr = m_baseAllocator->Allocate(size, alignment);
            if (!ptr) return nullptr;
            // Monotonic counters (allocation path)
            m_totalAllocCalls.fetch_add(1, std::memory_order_relaxed);
            m_totalBytesAllocated.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);

#if DNG_MEM_TRACKING
            // Full tracking mode: record allocation details
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                AllocationRecord record(size, alignment, info);
                m_allocations[ptr] = record;
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
        /**
         * @brief Get statistics for a specific allocation tag
         *
         * @param tag Allocation tag to query
         * @return Reference to statistics for the tag
         */
        [[nodiscard]] const AllocatorStats& GetStats(AllocTag tag) const noexcept {
            usize tagIndex = static_cast<usize>(tag);
            DNG_CHECK(tagIndex < static_cast<usize>(AllocTag::Count));
            return m_stats[tagIndex];
        }

        /**
         * @brief Reset all statistics to zero
         *
         * Clears all accumulated statistics but does not affect
         * active allocation tracking.
         */
        void ResetStats() noexcept {
            for (auto& stats : m_stats) {
                stats.Reset();
            }
        }
#endif // DNG_MEM_TRACKING_ENABLED

        /**
         * @brief Get the underlying base allocator
         *
         * @return Pointer to the wrapped allocator
         */
        [[nodiscard]] IAllocator* GetBaseAllocator() const noexcept {
            return m_baseAllocator;
        }

        // ---
        // Purpose : Capture an instantaneous per-tag aggregate suitable for
        //           leak snapshot comparisons.
        // Contract: Thread-safe; may be called concurrently with Allocate /
        //           Deallocate. Returns zeros when tracking/statistics support
        //           is compiled out.
        // Notes   : Relies solely on atomic counters (no heavy lock) to keep
        //           the capture path inexpensive even under contention.
        // ---
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

        // ---
        // Purpose : Return a point-in-time copy of cumulative counters.
        // Contract: Thread-safe; lock-free. Callable anytime.
        // Notes   : Used by Bench.hpp to compute bytes/op and allocs/op from deltas.
        // ---
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
        void ReportStatistics() const noexcept;

#if DNG_MEM_TRACKING
        void ReportLeaks() const noexcept;
        usize GetActiveAllocationCount() const noexcept;
#endif
    };

    /**
     * @brief RAII helper for automatic leak reporting
     *
     * This helper automatically calls ReportLeaks() on the wrapped
     * TrackingAllocator when it goes out of scope, ensuring leak
     * reports are generated even if manual reporting is forgotten.
     *
     * Only active when DNG_MEM_REPORT_ON_EXIT is enabled.
     */
#if DNG_MEM_TRACKING && DNG_MEM_REPORT_ON_EXIT
    class ReportOnExit {
    private:
        TrackingAllocator* m_allocator;

    public:
        explicit ReportOnExit(TrackingAllocator* allocator) noexcept
            : m_allocator(allocator) {
        }

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
    /**
     * @brief Create AllocInfo with automatic callsite capture
     *
     * Usage: DNG_ALLOC_INFO(AllocTag::Rendering, "Vertex Buffer")
     */
#define DNG_ALLOC_INFO(tag, name) \
        dng::core::AllocInfo(tag, name, __FILE__, __LINE__)
#else
#define DNG_ALLOC_INFO(tag, name) \
        dng::core::AllocInfo(tag, name)
#endif

    /**
     * @brief Tagged allocation with automatic callsite capture
     *
     * Usage: DNG_ALLOC_TAGGED(allocator, size, alignment, AllocTag::Rendering, "Vertex Buffer")
     */
#define DNG_ALLOC_TAGGED(allocator, size, alignment, tag, name) \
        (allocator)->AllocateTagged(size, alignment, DNG_ALLOC_INFO(tag, name))

} // namespace dng::core