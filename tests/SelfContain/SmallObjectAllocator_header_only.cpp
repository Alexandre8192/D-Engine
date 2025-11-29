// Compile-only self containment check for SmallObjectAllocator.hpp
#include "Core/Memory/SmallObjectAllocator.hpp"

namespace
{
    // Purpose: compile-time/self-containment check without needing an entry point.
    [[maybe_unused]] constexpr bool kSmallObjectAllocatorHeaderOnlyIncluded = true;
}
