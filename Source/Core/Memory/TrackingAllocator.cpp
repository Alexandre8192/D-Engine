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

#include "Core/Logger.hpp"

// Standard Library
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <type_traits>

// Provide a default for log verbosity if not defined by config.
#ifndef DNG_MEM_LOG_VERBOSITY
#define DNG_MEM_LOG_VERBOSITY 1
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
            if (Logger::IsEnabled(LogLevel::Info, "Memory")) {
                DNG_LOG_INFO("Memory", "{}", message);
            }
#else
            (void)message;
#endif
        }

        void LogMemoryWarning(const char* message) {
            if (Logger::IsEnabled(LogLevel::Warn, "Memory")) {
                DNG_LOG_WARNING("Memory", "{}", message);
            }
            else {
                (void)message;
            }
        }

        void LogMemoryError(const char* message) {
            if (Logger::IsEnabled(LogLevel::Error, "Memory")) {
                DNG_LOG_ERROR("Memory", "{}", message);
            }
            else {
                (void)message;
            }
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

        void FormatBytes(usize bytes, char* buffer, usize bufferSize) {
            const std::size_t destSize = static_cast<std::size_t>(bufferSize);
            if (!buffer || destSize == 0) {
                return;
            }
            const char* units[] = { "B", "KB", "MB", "GB", "TB" };
            double size = static_cast<double>(bytes);
            int unitIndex = 0;

            while (size >= 1024.0 && unitIndex < 4) {
                size /= 1024.0;
                ++unitIndex;
            }

            std::snprintf(buffer, destSize, "%.2f %s", size, units[unitIndex]);
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void TrackingAllocator::ReportStatistics() const noexcept {
#if DNG_MEM_TRACKING
        if (!Logger::IsEnabled(LogLevel::Info, "Memory")) {
            return;
        }
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

                char currentBuf[32];
                char peakBuf[32];
                char lineBuf[160];
                FormatBytes(currentBytes, currentBuf, sizeof(currentBuf));
                FormatBytes(peakBytes, peakBuf, sizeof(peakBuf));
                std::snprintf(
                    lineBuf,
                    sizeof(lineBuf),
                    "  %s: Current=%s (%llu allocs), Peak=%s, Total=%llu allocs",
                    tagName,
                    currentBuf,
                    static_cast<unsigned long long>(currentAllocs),
                    peakBuf,
                    static_cast<unsigned long long>(totalAllocs));

                LogMemoryInfo(lineBuf);

                totalCurrentBytes += currentBytes;
                totalPeakBytes += peakBytes;
                totalAllocations += totalAllocs;
                totalCurrentAllocations += currentAllocs;
            }
        }

        if (totalAllocations > 0) {
            char currentBuf[32];
            char peakBuf[32];
            char totalsBuf[160];
            FormatBytes(totalCurrentBytes, currentBuf, sizeof(currentBuf));
            FormatBytes(totalPeakBytes, peakBuf, sizeof(peakBuf));
            std::snprintf(
                totalsBuf,
                sizeof(totalsBuf),
                "TOTALS: Current=%s (%llu allocs), Peak=%s, Total=%llu allocs",
                currentBuf,
                static_cast<unsigned long long>(totalCurrentAllocations),
                peakBuf,
                static_cast<unsigned long long>(totalAllocations));
            LogMemoryInfo(totalsBuf);
        }
        else {
            LogMemoryInfo("No allocations tracked.");
        }

        LogMemoryInfo("=====================================");
#else
        if (!Logger::IsEnabled(LogLevel::Info, "Memory")) {
            return;
        }
        LogMemoryInfo("Memory tracking is disabled. Enable DNG_MEM_TRACKING for statistics.");
#endif
    }

#if DNG_MEM_TRACKING
    void TrackingAllocator::ReportLeaks() const noexcept {
        const bool infoEnabled = Logger::IsEnabled(LogLevel::Info, "Memory");
        const bool errorEnabled = Logger::IsEnabled(LogLevel::Error, "Memory");
        if (!infoEnabled && !errorEnabled) {
            return;
        }

        usize totalEntries = 0;
        VisitShardsConst([&](const AllocationShard& shard)
        {
            std::lock_guard<std::mutex> shardLock(shard.mutex);
            totalEntries += shard.allocations.size();
        });

        if (totalEntries == 0) {
            if (infoEnabled) {
                LogMemoryInfo("No memory leaks detected.");
            }
            return;
        }

        if (!errorEnabled) {
            return;
        }

        LogMemoryError("=== MEMORY LEAKS DETECTED ===");

        std::vector<AllocationRecord> collected;
        collected.reserve(static_cast<std::size_t>(totalEntries));

        VisitShardsConst([&](const AllocationShard& shard)
        {
            std::lock_guard<std::mutex> shardLock(shard.mutex);
            for (const auto& entry : shard.allocations)
            {
                collected.push_back(entry.second);
            }
        });

        usize totalLeakedBytes = 0;
        usize leakCount = 0;

        std::unordered_map<AllocTag, std::vector<const AllocationRecord*>, AllocTagHash> leaksByTag;
        leaksByTag.reserve(collected.size());

        for (const AllocationRecord& record : collected) {
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

            char leakedBuf[32];
            char tagHeader[160];
            FormatBytes(tagLeakedBytes, leakedBuf, sizeof(leakedBuf));
            std::snprintf(
                tagHeader,
                sizeof(tagHeader),
                "  %s leaks: %llu allocations, %s",
                tagName,
                static_cast<unsigned long long>(recs.size()),
                leakedBuf);
            LogMemoryError(tagHeader);

            // Limit detailed entries to avoid spamming logs
            constexpr usize kMaxReportedLeaks = 10;
            usize reported = 0;

            for (const AllocationRecord* rec : recs) {
                if (reported >= kMaxReportedLeaks) {
                    char remaining[96];
                    std::snprintf(
                        remaining,
                        sizeof(remaining),
                        "    ... and %llu more leaks",
                        static_cast<unsigned long long>(recs.size() - reported));
                    LogMemoryError(remaining);
                    break;
                }

                char sizeBuf[32];
                char leak[192];
                const char* name = rec->info.name ? rec->info.name : "<unnamed>";
                FormatBytes(rec->size, sizeBuf, sizeof(sizeBuf));
                int written = std::snprintf(
                    leak,
                    sizeof(leak),
                    "    - %s (%s)",
                    sizeBuf,
                    name);

#if DNG_MEM_CAPTURE_CALLSITE
                if (written > 0 && written < static_cast<int>(sizeof(leak)) && rec->info.file && rec->info.line > 0) {
                    std::snprintf(
                        leak + static_cast<std::size_t>(written),
                        sizeof(leak) - static_cast<std::size_t>(written),
                        " at %s:%u",
                        rec->info.file,
                        rec->info.line);
                }
#endif
                LogMemoryError(leak);
                ++reported;
            }
        }

        // Summary
        char leakedBuf[32];
        char summary[128];
        FormatBytes(totalLeakedBytes, leakedBuf, sizeof(leakedBuf));
        std::snprintf(
            summary,
            sizeof(summary),
            "TOTAL LEAKS: %llu allocations, %s",
            static_cast<unsigned long long>(leakCount),
            leakedBuf);
        LogMemoryError(summary);
        LogMemoryError("=============================");
    }

    usize TrackingAllocator::GetActiveAllocationCount() const noexcept {
        usize total = 0;
        VisitShardsConst([&](const AllocationShard& shard)
        {
            std::lock_guard<std::mutex> shardLock(shard.mutex);
            total += shard.allocations.size();
        });
        return total;
    }
#endif // DNG_MEM_TRACKING

} // namespace dng::core
