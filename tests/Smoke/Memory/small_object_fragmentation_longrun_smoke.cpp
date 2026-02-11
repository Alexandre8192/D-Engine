// ============================================================================
// SmallObject Fragmentation Long-Run Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Exercise long-running mixed-size allocate/free patterns to validate
//           SmallObjectAllocator behaviour under fragmentation pressure.
// Contract: Deterministic pseudo-random pattern; no exceptions; returns non-zero
//           on failure.
// ============================================================================

#if __has_include("Core/Memory/SmallObjectAllocator.hpp")
#    include "Core/Memory/SmallObjectAllocator.hpp"
#else
#    include "../../Source/Core/Memory/SmallObjectAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#if __has_include("Core/Memory/TrackingAllocator.hpp")
#    include "Core/Memory/TrackingAllocator.hpp"
#else
#    include "../../Source/Core/Memory/TrackingAllocator.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    struct LiveEntry
    {
        void* ptr{ nullptr };
        std::size_t size{ 0 };
        std::size_t alignment{ 0 };
    };

    [[nodiscard]] std::uint32_t NextValue(std::uint32_t& state) noexcept
    {
        state = (state * 1664525u) + 1013904223u;
        return state;
    }
} // namespace

int RunSmallObjectFragmentationLongRunSmoke()
{
    ::dng::core::DefaultAllocator parent{};
    ::dng::core::TrackingAllocator tracking(&parent);

    ::dng::core::SmallObjectConfig cfg{};
    cfg.EnableTLSBins = false;
    cfg.SlabSizeBytes = 64u * 1024u;
    cfg.MaxClassSize = 256u;
    cfg.ReturnNullOnOOM = true;
    cfg.ShardCountOverride = 8u;

    constexpr std::array<std::size_t, 10> kSizes = {
        8u, 16u, 24u, 32u, 48u, 64u, 96u, 128u, 192u, 256u
    };
    constexpr std::size_t kAlign = alignof(std::max_align_t);
    constexpr std::size_t kIterations = 30000u;

    std::size_t maxLive = 0u;
    std::vector<LiveEntry> live{};
    live.reserve(2048u);

    {
        ::dng::core::SmallObjectAllocator allocator(&tracking, cfg);
        std::uint32_t rng = 0xC0FFEEu;

        for (std::size_t i = 0; i < kIterations; ++i)
        {
            const std::uint32_t token = NextValue(rng);
            const bool doAlloc = live.empty() || ((token & 3u) != 0u);

            if (doAlloc)
            {
                const std::size_t size = kSizes[token % static_cast<std::uint32_t>(kSizes.size())];
                void* const ptr = allocator.Allocate(size, kAlign);
                if (!ptr)
                {
                    return 1;
                }
                live.push_back(LiveEntry{ ptr, size, kAlign });
                if (live.size() > maxLive)
                {
                    maxLive = live.size();
                }
            }
            else
            {
                const std::size_t idx = static_cast<std::size_t>(token % static_cast<std::uint32_t>(live.size()));
                const LiveEntry entry = live[idx];
                allocator.Deallocate(entry.ptr, entry.size, entry.alignment);
                live[idx] = live.back();
                live.pop_back();
            }
        }

        for (const LiveEntry& entry : live)
        {
            allocator.Deallocate(entry.ptr, entry.size, entry.alignment);
        }
        live.clear();
    }

    if (maxLive < 256u)
    {
        return 2;
    }

#if DNG_MEM_TRACKING
    if (tracking.GetActiveAllocationCount() != 0u)
    {
        return 3;
    }
#endif

    return 0;
}
