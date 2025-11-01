// Compile-only self containment check for ArenaAllocator.hpp
#include "Core/Memory/ArenaAllocator.hpp"

namespace {
    void TouchArenaAllocatorHeaderOnly() noexcept
    {
        using AllocType = ::dng::core::ArenaAllocator;
        (void)sizeof(AllocType*);
    }
}

static_assert(true, "ArenaAllocator header-only TU compiles");
