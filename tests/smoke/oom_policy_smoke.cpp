// ============================================================================
// OOM Policy Smoke Test (compile-only)
// ----------------------------------------------------------------------------
// Ensures the global OOM helpers compile in isolation.
// ============================================================================

#if __has_include("Core/Memory/OOM.hpp")
#    include "Core/Memory/OOM.hpp"
#else
#    include "../../Source/Core/Memory/OOM.hpp"
#endif

#if 0
#include <cstddef>

static void OOMSmoke()
{
    constexpr std::size_t size = 128;
    constexpr std::size_t alignment = alignof(std::max_align_t);
    DNG_MEM_CHECK_OOM(size, alignment, "OOMSmoke");
}
#endif
