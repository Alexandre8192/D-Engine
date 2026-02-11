// ============================================================================
// PoolAllocator Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Validate fixed-size allocation/deallocation flow and free-count
//           accounting for PoolAllocator.
// Contract: No exceptions; deterministic; returns non-zero on failure.
// ============================================================================

#if __has_include("Core/Memory/PoolAllocator.hpp")
#    include "Core/Memory/PoolAllocator.hpp"
#else
#    include "../../Source/Core/Memory/PoolAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#include <array>
#include <cstddef>

int RunPoolAllocatorSmoke()
{
    ::dng::core::DefaultAllocator parent{};
    constexpr ::dng::core::usize kBlockSize = 64u;
    constexpr ::dng::core::usize kBlockAlign = alignof(std::max_align_t);
    constexpr ::dng::core::usize kBlockCount = 8u;

    ::dng::core::PoolAllocator pool(&parent, kBlockSize, kBlockAlign, kBlockCount);
    if (pool.GetTotalBlocks() == 0u)
    {
        return 1;
    }

    std::array<void*, kBlockCount> blocks{};
    for (::dng::core::usize i = 0; i < kBlockCount; ++i)
    {
        blocks[i] = pool.Allocate(kBlockSize, kBlockAlign);
        if (!blocks[i])
        {
            return 2;
        }
    }

    if (pool.GetAvailableBlocks() != 0u)
    {
        return 3;
    }

    for (::dng::core::usize i = 0; i < kBlockCount; ++i)
    {
        pool.Deallocate(blocks[i], kBlockSize, kBlockAlign);
    }

    if (pool.GetAvailableBlocks() != pool.GetTotalBlocks())
    {
        return 4;
    }

    return 0;
}
