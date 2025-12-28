#pragma once
// ============================================================================
// D-Engine - LeakSnapshots.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a header-only, diagnostic-oriented API to capture and diff
//           memory usage snapshots from the TrackingAllocator. Enables leak
//           detection, regression triage, and high-level memory evolution
//           reports between phases (e.g., LoadLevel, Gameplay, Shutdown).
//
// Contract: 
//   - Requires the MemorySystem to be initialized (checked via DNG_MEMORY_INIT_GUARD).
//   - Requires DNG_MEM_TRACKING != 0 to gather live stats; otherwise this header
//     gracefully degrades to a no-op and logs a warning.
//   - Thread-safe for concurrent capture, purely read-only.
//   - Allocations are temporary and made via standard STL containers.
//
// Notes   :
//   - Header-only and self-contained (no .cpp).
//   - Produces deterministic, lexicographically sorted reports for CI logs.
//   - Intended for developer diagnostics; do not ship in production builds.
//   - Future extensions: ToCSV(), ToJSON(), rolling snapshot history.
// ============================================================================

#include "Core/CoreMinimal.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/MemorySystem.hpp"
#include "Core/Memory/TrackingAllocator.hpp"

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <type_traits>

// ----------------------------------------------------------------------------
// Log category for all memory snapshot operations
// ----------------------------------------------------------------------------
#ifndef DNG_LEAK_SNAPSHOT_LOG_CATEGORY
#define DNG_LEAK_SNAPSHOT_LOG_CATEGORY "Memory.LeakSnapshots"
#endif

// ----------------------------------------------------------------------------
// Compatibility guard for legacy macro naming
// ----------------------------------------------------------------------------
#ifndef DNG_MEM_TRACKING_ENABLED
#  ifdef DNG_MEM_TRACKING
#    define DNG_MEM_TRACKING_ENABLED DNG_MEM_TRACKING
#  else
#    define DNG_MEM_TRACKING_ENABLED 0
#  endif
#endif

namespace dng::memory
{
    // ------------------------------------------------------------------------
    // SnapshotTagStats
    // ------------------------------------------------------------------------
    // Purpose : Aggregated bytes / allocation counters per tag at capture time.
    // Contract: Represents instantaneous state, not cumulative history.
    // Notes   : Used both within captured snapshots and diff structures.
    // ------------------------------------------------------------------------
    struct SnapshotTagStats
    {
        std::size_t bytes{0};
        std::size_t allocs{0};
    };

    namespace detail
    {
        // --------------------------------------------------------------------
        // Purpose : Convert an AllocTag enum into a human-readable string label.
        // Contract: Exhaustive for all enum values. Defaults to "Unknown".
        // Notes   : Mirrors TrackingAllocator reporting for naming consistency.
        // --------------------------------------------------------------------
        [[nodiscard]] inline const char* AllocTagToString(::dng::core::AllocTag tag) noexcept
        {
            using ::dng::core::AllocTag;
            switch (tag)
            {
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
                case AllocTag::Count:       break;
            }
            return "Unknown";
        }

        // Sanity check: keep this in sync with the enum.
        static_assert(static_cast<size_t>(::dng::core::AllocTag::Count) >= 10,
                      "AllocTagToString likely needs update after new tags.");

        // --------------------------------------------------------------------
        // Purpose : Format a signed delta (+/-) as a human-readable string.
        // Contract: Always includes a sign for nonzero; 0 prints as '0'.
        // --------------------------------------------------------------------
        [[nodiscard]] inline std::string FormatSigned(std::ptrdiff_t value)
        {
            std::ostringstream oss;
            if (value > 0)       oss << '+' << value;
            else if (value < 0)  oss << value;
            else                 oss << '0';
            return oss.str();
        }

        // --------------------------------------------------------------------
        // Purpose : Convert signed delta to absolute size_t without UB.
        // Contract: Two's-complement arithmetic assumed.
        // --------------------------------------------------------------------
        [[nodiscard]] inline std::size_t AbsToSize(std::ptrdiff_t value) noexcept
        {
            using Unsigned = std::make_unsigned_t<std::ptrdiff_t>;
            if (value >= 0) return static_cast<std::size_t>(value);
            const Unsigned magnitude = static_cast<Unsigned>(~value) + 1u;
            return static_cast<std::size_t>(magnitude);
        }

        // --------------------------------------------------------------------
        // Purpose : Copy unordered_map entries into a sorted vector by key.
        // Contract: Deterministic ordering; complexity O(N log N).
        // --------------------------------------------------------------------
        template <typename MapT>
        [[nodiscard]] inline std::vector<std::pair<std::string, typename MapT::mapped_type>>
        MapToSortedVector(const MapT& map)
        {
            std::vector<std::pair<std::string, typename MapT::mapped_type>> v;
            v.reserve(map.size());
            for (const auto& kv : map) v.emplace_back(kv.first, kv.second);
            std::sort(v.begin(), v.end(),
                      [](const auto& a, const auto& b){ return a.first < b.first; });
            return v;
        }

        // --------------------------------------------------------------------
        // Purpose : Compute widest tag name for table alignment.
        // --------------------------------------------------------------------
        [[nodiscard]] inline std::size_t ComputeMaxTagWidth(
            const std::unordered_map<std::string, SnapshotTagStats>& a,
            const std::unordered_map<std::string, SnapshotTagStats>& b,
            const std::unordered_map<std::string, SnapshotTagStats>& c)
        {
            std::size_t width = 0;
            auto check = [&width](const auto& map){
                for (const auto& kv : map) width = std::max(width, kv.first.size());
            };
            check(a); check(b); check(c);
            return width;
        }
    } // namespace detail

    // ------------------------------------------------------------------------
    // Snapshot
    // ------------------------------------------------------------------------
    // Purpose : Immutable capture of current tracked allocation footprint.
    // Contract: `name` is a borrowed pointer (string literal recommended).
    // Notes   : Monotonic `stamp` aids chronological ordering in reports.
    // ------------------------------------------------------------------------
    struct Snapshot
    {
        const char*   name{nullptr};
        std::uint64_t stamp{0};
        std::unordered_map<std::string, SnapshotTagStats> byTag;
        std::size_t totalBytes{0};
        std::size_t totalAllocs{0};
    };

    // ------------------------------------------------------------------------
    // SnapshotDiff
    // ------------------------------------------------------------------------
    // Purpose : Represents the difference between two snapshots.
    // Contract: Both snapshots must originate from the same execution.
    // Notes   : Sign and magnitude are stored separately for clean formatting.
    // ------------------------------------------------------------------------
    struct SnapshotDiff
    {
        const char* fromName{nullptr};
        const char* toName{nullptr};
        std::unordered_map<std::string, SnapshotTagStats> added;
        std::unordered_map<std::string, SnapshotTagStats> removed;
        std::unordered_map<std::string, SnapshotTagStats> changed;
        std::unordered_map<std::string, std::pair<int,int>> changedSigns;
        std::ptrdiff_t deltaBytes{0};
        std::ptrdiff_t deltaAllocs{0};
        std::size_t fromTotalBytes{0}, fromTotalAllocs{0};
        std::size_t toTotalBytes{0},   toTotalAllocs{0};

        // --------------------------------------------------------------------
        // Purpose : Produce a multi-line, deterministic text diff for logs.
        // Contract: Returns an empty string when the diff is trivial (no
        //           ADDED/REMOVED/CHANGED entries and zero aggregate deltas).
        // Notes   : All sections are lexicographically sorted for readability.
        // --------------------------------------------------------------------
        [[nodiscard]] std::string ToString() const
        {
            // Bail out early when nothing changed between snapshots.
            if (added.empty() && removed.empty() && changed.empty()
                && deltaBytes == 0 && deltaAllocs == 0)
            {
                return {};
            }

            std::ostringstream oss;
            oss << "SnapshotDiff from '" << (fromName ? fromName : "<null>")
                << "' to '" << (toName ? toName : "<null>") << "'\n";
            oss << "Totals:\n";
            oss << "  bytes : " << toTotalBytes << " (" << detail::FormatSigned(deltaBytes) << ")\n";
            oss << "  allocs: " << toTotalAllocs << " (" << detail::FormatSigned(deltaAllocs) << ")\n\n";

            const std::size_t tagWidth = detail::ComputeMaxTagWidth(added, removed, changed);
            const int colW = static_cast<int>(tagWidth);

            auto emitSection = [&](const char* header,
                                const std::unordered_map<std::string, SnapshotTagStats>& map,
                                auto deltaResolver)
            {
                oss << header << ":\n";
                if (map.empty()) { oss << "  <none>\n\n"; return; }

                const auto entries = detail::MapToSortedVector(map);
                oss << "  " << std::left << std::setw(colW) << "Tag"
                    << "  Bytes (delta)   Allocs (delta)\n";
                for (const auto& [tag, stats] : entries)
                {
                    const auto [db, da] = deltaResolver(tag, stats);
                    oss << "  " << std::left << std::setw(colW) << tag
                        << "  " << std::right << std::setw(10) << detail::FormatSigned(db)
                        << "   " << std::setw(10) << detail::FormatSigned(da) << "\n";
                }
                oss << "\n";
            };

            emitSection("ADDED", added,
                [](const std::string&, const SnapshotTagStats& s){
                    return std::make_pair(static_cast<std::ptrdiff_t>(s.bytes),
                                        static_cast<std::ptrdiff_t>(s.allocs));
                });
            emitSection("REMOVED", removed,
                [](const std::string&, const SnapshotTagStats& s){
                    return std::make_pair(-static_cast<std::ptrdiff_t>(s.bytes),
                                        -static_cast<std::ptrdiff_t>(s.allocs));
                });
            emitSection("CHANGED", changed,
                [this](const std::string& tag, const SnapshotTagStats& s){
                    const auto it = changedSigns.find(tag);
                    int sb = 0, sa = 0;
                    if (it != changedSigns.end()) { sb = it->second.first; sa = it->second.second; }
                    return std::make_pair(static_cast<std::ptrdiff_t>(s.bytes) * sb,
                                        static_cast<std::ptrdiff_t>(s.allocs) * sa);
                });

            return oss.str();
        }
    };

    // ------------------------------------------------------------------------
    // LeakSnapshots facade
    // ------------------------------------------------------------------------
    // Purpose : Capture and diff global tracking state at runtime.
    // Contract: Requires MemorySystem initialized. Name must outlive Snapshot.
    // Notes   : Safe no-op when tracking is disabled; logs warnings accordingly.
    // ------------------------------------------------------------------------
    struct LeakSnapshots
    {
        [[nodiscard]] static Snapshot Capture(const char* name)
        {
            DNG_MEMORY_INIT_GUARD();
            Snapshot snap{};
            snap.name = name;

            static std::atomic<std::uint64_t> sStamp{1};
            snap.stamp = sStamp.fetch_add(1, std::memory_order_relaxed);

#if !DNG_MEM_TRACKING_ENABLED
            DNG_LOG_WARNING(DNG_LEAK_SNAPSHOT_LOG_CATEGORY,
                            "LeakSnapshots::Capture ignored: tracking compiled out.");
            return snap;
#else
            const auto& config = ::dng::core::MemoryConfig::GetGlobal();
            if (!config.enable_tracking)
            {
                DNG_LOG_WARNING(DNG_LEAK_SNAPSHOT_LOG_CATEGORY,
                                "LeakSnapshots::Capture ignored: tracking disabled at runtime.");
                return snap;
            }

            const auto trackingRef = MemorySystem::GetTrackingAllocator();
            auto* tracking = static_cast<::dng::core::TrackingAllocator*>(trackingRef.Get());
            if (!tracking)
            {
                DNG_LOG_WARNING(DNG_LEAK_SNAPSHOT_LOG_CATEGORY,
                                "LeakSnapshots::Capture failed: TrackingAllocator unavailable.");
                return snap;
            }

            const auto view = tracking->CaptureView();
            snap.totalBytes  = view.totalBytes;
            snap.totalAllocs = view.totalAllocs;

            for (std::size_t i = 0; i < view.byTag.size(); ++i)
            {
                const auto& tagSample = view.byTag[i];
                if (tagSample.bytes == 0 && tagSample.allocs == 0)
                    continue;

                const auto tagEnum = static_cast<::dng::core::AllocTag>(i);
                const char* tagName = detail::AllocTagToString(tagEnum);
                snap.byTag.emplace(tagName, SnapshotTagStats{tagSample.bytes, tagSample.allocs});
            }

            return snap;
#endif
        }

        [[nodiscard]] static SnapshotDiff Diff(const Snapshot& from, const Snapshot& to)
        {
            SnapshotDiff diff{};
            diff.fromName = from.name;
            diff.toName   = to.name;

            diff.fromTotalBytes  = from.totalBytes;
            diff.fromTotalAllocs = from.totalAllocs;
            diff.toTotalBytes    = to.totalBytes;
            diff.toTotalAllocs   = to.totalAllocs;
            diff.deltaBytes  = static_cast<std::ptrdiff_t>(to.totalBytes) - static_cast<std::ptrdiff_t>(from.totalBytes);
            diff.deltaAllocs = static_cast<std::ptrdiff_t>(to.totalAllocs) - static_cast<std::ptrdiff_t>(from.totalAllocs);

            for (const auto& [tag, toStats] : to.byTag)
            {
                const auto it = from.byTag.find(tag);
                if (it == from.byTag.end())
                {
                    diff.added.emplace(tag, toStats);
                    continue;
                }

                const auto& fromStats = it->second;
                const std::ptrdiff_t db = static_cast<std::ptrdiff_t>(toStats.bytes)  - static_cast<std::ptrdiff_t>(fromStats.bytes);
                const std::ptrdiff_t da = static_cast<std::ptrdiff_t>(toStats.allocs) - static_cast<std::ptrdiff_t>(fromStats.allocs);
                if (db != 0 || da != 0)
                {
                    diff.changed[tag] = SnapshotTagStats{ detail::AbsToSize(db), detail::AbsToSize(da) };
                    diff.changedSigns[tag] = { (db > 0) - (db < 0), (da > 0) - (da < 0) };
                }
            }

            for (const auto& [tag, fromStats] : from.byTag)
            {
                if (to.byTag.find(tag) != to.byTag.end())
                    continue;
                diff.removed.emplace(tag, fromStats);
            }

            return diff;
        }
    };

} // namespace dng::memory
