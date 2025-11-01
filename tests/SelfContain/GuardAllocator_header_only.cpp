// Compile-only self containment check for GuardAllocator.hpp
#include "Core/Memory/GuardAllocator.hpp"

namespace {
    void TouchGuardAllocatorHeaderOnly() noexcept
    {
        using GuardAlloc = ::dng::memory::GuardAllocator;
        (void)sizeof(GuardAlloc*);
    }
}

static_assert(true, "GuardAllocator header-only TU compiles");
