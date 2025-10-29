// ============================================================================
// Tracking Allocator Smoke Test (compile-only)
// ----------------------------------------------------------------------------
// Ensures the public TrackingAllocator headers compile in isolation.
// ============================================================================

#if __has_include("Core/CoreMinimal.hpp")
#    include "Core/CoreMinimal.hpp"
#else
#    include "../../Source/Core/CoreMinimal.hpp"
#endif

#if 0
using namespace dng::core;

static void TrackingAllocatorSmoke(TrackingAllocator& tracking)
{
    AllocInfo info{ AllocTag::General, "Smoke" };
    void* ptr = tracking.AllocateTagged(64, alignof(std::max_align_t), info);
    tracking.Deallocate(ptr, 64, alignof(std::max_align_t));
}
#endif
