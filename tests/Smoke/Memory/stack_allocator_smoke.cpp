// ============================================================================
// StackAllocator Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Ensure StackAllocator header remains self-contained and supports
//           basic push/pop usage with alignment guarantees.
// ============================================================================

#if __has_include("Core/Diagnostics/Check.hpp")
#    include "Core/Diagnostics/Check.hpp"
#else
#    include "../../Source/Core/Diagnostics/Check.hpp"
#endif

#if __has_include("Core/Memory/StackAllocator.hpp")
#    include "Core/Memory/StackAllocator.hpp"
#else
#    include "../../Source/Core/Memory/StackAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#include <cstdint>
#include <cstddef>

int RunStackAllocatorSmoke()
{
    ::dng::core::DefaultAllocator parent{};
    constexpr ::dng::core::usize kCapacity = 256;
    ::dng::core::StackAllocator stack(&parent, kCapacity);

    auto markerA = stack.Push(32, 16);
    DNG_CHECK(markerA.IsValid());

    ::dng::core::StackMarker markerB{};
    void* ptr = stack.PushAndGetPointer(48, 32, markerB);
    DNG_CHECK(ptr != nullptr && markerB.IsValid());

    stack.Pop(markerB);
    stack.Pop(markerA);

    stack.Reset();
    DNG_CHECK(stack.GetStackDepth() == 0);

#ifndef NDEBUG
    constexpr ::dng::core::usize kMarkerLimit = ::dng::core::CompiledStackAllocatorMaxMarkers();
    constexpr ::dng::core::usize kOverflowPushes = kMarkerLimit + 8;
    constexpr ::dng::core::usize kOverflowCapacity =
        (kOverflowPushes + 2) * alignof(std::max_align_t);

    ::dng::core::StackAllocator overflowStack(&parent, kOverflowCapacity);
    for (::dng::core::usize i = 0; i < kOverflowPushes; ++i)
    {
        const ::dng::core::StackMarker marker = overflowStack.Push(1, 1);
        DNG_CHECK(marker.IsValid());
    }

    DNG_CHECK(overflowStack.GetStackDepth() == kMarkerLimit);
    overflowStack.Reset();
    DNG_CHECK(overflowStack.GetStackDepth() == 0);
#endif

    return 0;
}
