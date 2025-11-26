// ============================================================================
// ArenaAllocator Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Include ArenaAllocator in isolation and exercise basic bump usage.
// Contract: Allocates a few blocks, rewinds with markers, and validates stats.
// ============================================================================

#if __has_include("Core/Diagnostics/Check.hpp")
#    include "Core/Diagnostics/Check.hpp"
#else
#    include "../../Source/Core/Diagnostics/Check.hpp"
#endif

#if __has_include("Core/Memory/ArenaAllocator.hpp")
#    include "Core/Memory/ArenaAllocator.hpp"
#else
#    include "../../Source/Core/Memory/ArenaAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#if __has_include("Core/Memory/Alignment.hpp")
#    include "Core/Memory/Alignment.hpp"
#else
#    include "../../Source/Core/Memory/Alignment.hpp"
#endif

#include <cstdint>

int main()
{
    ::dng::core::DefaultAllocator parent{};
    constexpr ::dng::core::usize kCapacity = 512;
    ::dng::core::ArenaAllocator arena(&parent, kCapacity);

    DNG_CHECK(arena.IsValid());
    DNG_CHECK(arena.GetCapacity() == kCapacity);

    void* first = arena.Allocate(64, 16);
    DNG_CHECK(first != nullptr && arena.Owns(first));
    DNG_CHECK(::dng::core::IsAligned(reinterpret_cast<std::uintptr_t>(first), 16));

    ::dng::core::ArenaMarker marker = arena.GetMarker();
    void* second = arena.Allocate(32, 8);
    DNG_CHECK(second != nullptr && arena.Owns(second));

    arena.Rewind(marker);
    DNG_CHECK(arena.GetUsed() <= kCapacity / 2);

    arena.Reset();
    DNG_CHECK(arena.GetUsed() == 0);

    return 0;
}
