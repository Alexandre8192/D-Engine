// ============================================================================
// FrameAllocator Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Validate FrameAllocator basic bump/reset behaviour over
//           caller-supplied storage.
// ============================================================================

#if __has_include("Core/Diagnostics/Check.hpp")
#    include "Core/Diagnostics/Check.hpp"
#else
#    include "../../Source/Core/Diagnostics/Check.hpp"
#endif

#if __has_include("Core/Memory/FrameAllocator.hpp")
#    include "Core/Memory/FrameAllocator.hpp"
#else
#    include "../../Source/Core/Memory/FrameAllocator.hpp"
#endif

#include <array>
#include <cstdint>

int RunFrameAllocatorSmoke()
{
    alignas(64) std::array<std::uint8_t, 512> backing{};
    ::dng::core::FrameAllocator allocator(backing.data(), backing.size());

    void* first = allocator.Allocate(64, 32);
    DNG_CHECK(first != nullptr);

    auto marker = allocator.GetMarker();
    void* second = allocator.Allocate(96, 16);
    DNG_CHECK(second != nullptr);

    allocator.Rewind(marker);
    allocator.Reset();
    DNG_CHECK(allocator.GetUsed() == 0);

    return 0;
}
