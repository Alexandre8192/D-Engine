// ============================================================================
// Tracking Allocator Smoke Test
// ----------------------------------------------------------------------------
// Ensures the public TrackingAllocator headers compile in isolation and
// validates a basic allocate/deallocate flow.
// ============================================================================

#if __has_include("Core/Memory/TrackingAllocator.hpp")
#    include "Core/Memory/TrackingAllocator.hpp"
#else
#    include "../../Source/Core/Memory/TrackingAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#include <cstddef>

int RunTrackingAllocatorSmoke()
{
    ::dng::core::DefaultAllocator parent{};
    ::dng::core::TrackingAllocator tracking(&parent);

    constexpr std::size_t kSize = 64u;
    constexpr std::size_t kAlign = alignof(std::max_align_t);
    const ::dng::core::AllocInfo info{ ::dng::core::AllocTag::General, "TrackingSmoke" };

    void* ptr = tracking.AllocateTagged(kSize, kAlign, info);
    if (!ptr)
    {
        return 1;
    }

#if DNG_MEM_TRACKING
    if (tracking.GetActiveAllocationCount() == 0u)
    {
        tracking.Deallocate(ptr, kSize, kAlign);
        return 2;
    }
#endif

    tracking.Deallocate(ptr, kSize, kAlign);

#if DNG_MEM_TRACKING
    if (tracking.GetActiveAllocationCount() != 0u)
    {
        return 3;
    }
#endif

    return 0;
}
