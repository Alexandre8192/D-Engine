// ============================================================================
// D-Engine -- Core Memory
// File: Core/Memory/TrackingAllocator.cpp
// ----------------------------------------------------------------------------
// PURPOSE
// -------
// Implementation of reporting utilities for TrackingAllocator:
//   - ReportStatistics(): print aggregate per-tag and total allocation stats
//   - ReportLeaks():      print detailed leak report grouped by allocation tag
//
// SCOPE & NON-GOALS
// ------------------
// - This file does not change allocation logic; it only reports on state
//   collected elsewhere (the header / allocator implementation).
// - No new features are added; logging remains conditional on project macros.
//
// BUILD ASSUMPTIONS
// -----------------
// - Logging macros (DNG_LOG_INFO/WARNING/ERROR) might not be available at the
//   time this TU is compiled. We provide safe no-op fallbacks.
// - Memory configuration macros (DNG_MEM_TRACKING, DNG_MEM_LOG_VERBOSITY,
//   DNG_MEM_LOG_CATEGORY, DNG_MEM_CAPTURE_CALLSITE) may be defined by
//   MemoryConfig.hpp. We include it for consistency, but we still guard usage.
//
// THREAD SAFETY
// -------------
// - ReportStatistics(): reads atomic counters; no lock required.
// - ReportLeaks(): locks the allocation map to traverse active records safely.
//
// ============================================================================

#include "Core/Memory/TrackingAllocator.hpp"

// Ensure memory config/log categories if they exist
#include "Core/Memory/MemoryConfig.hpp"     // DNG_MEM_TRACKING, DNG_MEM_LOG_VERBOSITY, etc. (if available)
#include "Core/Logger.hpp"                  // Logging macros (if available)

// Standard Library
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <type_traits>

// -----------------------------------------------------------------------------
// Fallback logging macros (no-ops) if not provided elsewhere.
// These keep this TU buildable in isolation without forcing logger linkage.
// -----------------------------------------------------------------------------
#ifndef DNG_LOG_INFO
#define DNG_LOG_INFO(category, fmt, ...)    ((void)0)
#endif
#ifndef DNG_LOG_WARNING
#define DNG_LOG_WARNING(category, fmt, ...) ((void)0)
#endif
#ifndef DNG_LOG_ERROR
#define DNG_LOG_ERROR(category, fmt, ...)   ((void)0)
#endif

// Provide a default for log verbosity if not defined by config.
#ifndef DNG_MEM_LOG_VERBOSITY
#define DNG_MEM_LOG_VERBOSITY 1
#endif

// Provide a default log category token if not defined.
#ifndef DNG_MEM_LOG_CATEGORY
#define DNG_MEM_LOG_CATEGORY "Memory"
#endif

namespace dng::core {

    namespace {
        // ---------------------------------------------------------------------
        // Local hasher for enum class AllocTag (portability across libstdc++/MSVC)
        // ---------------------------------------------------------------------
        struct AllocTagHash {
            std::size_t operator()(AllocTag tag) const noexcept {
                using UT = std::underlying_type_t<AllocTag>;
                return static_cast<std::size_t>(static_cast<UT>(tag));
            }
        };

        // ---------------------------------------------------------------------
        // Lightweight logging wrappers (respect project verbosity policy)
        // ---------------------------------------------------------------------
        void LogMemoryInfo(const char* message) {
#if DNG_MEM_LOG_VERBOSITY >= 1
            DNG_LOG_INFO(DNG_MEM_LOG_CATEGORY, "%s", message);
#else
            (void)message;
#endif
        }

        void LogMemoryWarning(const char* message) {
            DNG_LOG_WARNING(DNG_MEM_LOG_CATEGORY, "%s", message);
        }

        void LogMemoryError(const char* message) {
            DNG_LOG_ERROR(DNG_MEM_LOG_CATEGORY, "%s", message);
        }

        // ---------------------------------------------------------------------
        // Pretty-print helpers
        // ---------------------------------------------------------------------
        const char* AllocTagToString(AllocTag tag) {
            switch (tag) {
            case AllocTag::General:     return "General";
            case AllocTag::Temporary:   return "Temporary";
            case AllocTag::Persistent:  return "Persistent";
            case AllocTag::Rendering:   return "Rendering";
            case AllocTag::Audio:       return "Audio";
            case AllocTag::Networking:  return "Networking";
            case AllocTag::Physics:     return "Physics";
            case AllocTag::Scripting:   return "Scripting";
            case AllocTag::Assets:      return "Assets";
            case AllocTag::UI:          return "UI";
            default:                    return "Unknown";
            }
        }

        std::string FormatBytes(usize bytes) {
            const char* units[] = { "B", "KB", "MB", "GB", "TB" };
            double size = static_cast<double>(bytes);
            int unitIndex = 0;

            while (size >= 1024.0 && unitIndex < 4) {
                size /= 1024.0;
                ++unitIndex;
            }

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
            return oss.str();
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void TrackingAllocator::ReportStatistics() const noexcept {
#if DNG_MEM_TRACKING
        LogMemoryInfo("=== Memory Allocation Statistics ===");

        usize totalCurrentBytes = 0;
        usize totalPeakBytes = 0;
        usize totalAllocations = 0;
        usize totalCurrentAllocations = 0;

        // Per-tag stats
        for (usize i = 0; i < static_cast<usize>(AllocTag::Count); ++i) {
            const AllocatorStats& stats = m_stats[i];

            const usize currentBytes = stats.current_bytes.load();
            const usize peakBytes = stats.peak_bytes.load();
            const usize totalAllocs = stats.total_allocations.load();
            const usize currentAllocs = stats.current_allocations.load();

            if (totalAllocs > 0) {
                const char* tagName = AllocTagToString(static_cast<AllocTag>(i));

                std::ostringstream line;
                line << "  " << tagName << ": "
                    << "Current=" << FormatBytes(currentBytes)
                    << " (" << currentAllocs << " allocs), "
                    << "Peak=" << FormatBytes(peakBytes)
                    << ", Total=" << totalAllocs << " allocs";

                LogMemoryInfo(line.str().c_str());

                totalCurrentBytes += currentBytes;
                totalPeakBytes += peakBytes;
                totalAllocations += totalAllocs;
                totalCurrentAllocations += currentAllocs;
            }
        }

        if (totalAllocations > 0) {
            std::ostringstream totals;
            totals << "TOTALS: Current=" << FormatBytes(totalCurrentBytes)
                << " (" << totalCurrentAllocations << " allocs), "
                << "Peak=" << FormatBytes(totalPeakBytes)
                << ", Total=" << totalAllocations << " allocs";
            LogMemoryInfo(totals.str().c_str());
        }
        else {
            LogMemoryInfo("No allocations tracked.");
        }

        LogMemoryInfo("=====================================");
#else
        LogMemoryInfo("Memory tracking is disabled. Enable DNG_MEM_TRACKING for statistics.");
#endif
    }

#if DNG_MEM_TRACKING
    void TrackingAllocator::ReportLeaks() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_allocations.empty()) {
            LogMemoryInfo("No memory leaks detected.");
            return;
        }

        LogMemoryError("=== MEMORY LEAKS DETECTED ===");

        usize totalLeakedBytes = 0;
        usize leakCount = 0;

        // Group leaks by tag for better reporting (hash is local and portable)
        std::unordered_map<AllocTag, std::vector<const AllocationRecord*>, AllocTagHash> leaksByTag;
        leaksByTag.reserve(m_allocations.size());

        for (const auto& pair : m_allocations) {
            const AllocationRecord& record = pair.second;
            leaksByTag[record.info.tag].push_back(&record);
            totalLeakedBytes += record.size;
            ++leakCount;
        }

        // Report leaks per tag
        for (const auto& tagPair : leaksByTag) {
            const AllocTag tag = tagPair.first;
            const auto& recs = tagPair.second;

            const char* tagName = AllocTagToString(tag);

            usize tagLeakedBytes = 0;
            for (const AllocationRecord* rec : recs) {
                tagLeakedBytes += rec->size;
            }

            std::ostringstream tagHeader;
            tagHeader << "  " << tagName << " leaks: "
                << recs.size() << " allocations, "
                << FormatBytes(tagLeakedBytes);
            LogMemoryError(tagHeader.str().c_str());

            // Limit detailed entries to avoid spamming logs
            constexpr usize kMaxReportedLeaks = 10;
            usize reported = 0;

            for (const AllocationRecord* rec : recs) {
                if (reported >= kMaxReportedLeaks) {
                    std::ostringstream remaining;
                    remaining << "    ... and " << (recs.size() - reported) << " more leaks";
                    LogMemoryError(remaining.str().c_str());
                    break;
                }

                std::ostringstream leak;
                leak << "    - " << FormatBytes(rec->size)
                    << " (" << rec->info.name << ")";

#if DNG_MEM_CAPTURE_CALLSITE
                if (rec->info.file && rec->info.line > 0) {
                    leak << " at " << rec->info.file << ":" << rec->info.line;
                }
#endif
                LogMemoryError(leak.str().c_str());
                ++reported;
            }
        }

        // Summary
        std::ostringstream summary;
        summary << "TOTAL LEAKS: " << leakCount << " allocations, "
            << FormatBytes(totalLeakedBytes);
        LogMemoryError(summary.str().c_str());
        LogMemoryError("=============================");
    }

    usize TrackingAllocator::GetActiveAllocationCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_allocations.size();
    }
#endif // DNG_MEM_TRACKING

} // namespace dng::core
