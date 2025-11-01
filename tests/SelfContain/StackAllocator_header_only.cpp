// Compile-only self containment check for StackAllocator.hpp
#include "Core/Memory/StackAllocator.hpp"

namespace {
    void TouchStackAllocatorHeaderOnly() noexcept
    {
        using AllocType = ::dng::core::StackAllocator;
        (void)sizeof(AllocType*);
    }
}

static_assert(true, "StackAllocator header-only TU compiles");
