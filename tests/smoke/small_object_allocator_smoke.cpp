// ============================================================================
// SmallObjectAllocator Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Exercise basic allocate/deallocate flow for SmallObjectAllocator
//           using a deterministic config without TLS bins.
// ============================================================================

#if __has_include("Core/Diagnostics/Check.hpp")
#    include "Core/Diagnostics/Check.hpp"
#else
#    include "../../Source/Core/Diagnostics/Check.hpp"
#endif

#if __has_include("Core/Memory/SmallObjectAllocator.hpp")
#    include "Core/Memory/SmallObjectAllocator.hpp"
#else
#    include "../../Source/Core/Memory/SmallObjectAllocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#include <cstdint>

int main()
{
    ::dng::core::DefaultAllocator parent{};

    ::dng::core::SmallObjectConfig cfg{};
    cfg.EnableTLSBins = false;
    cfg.SlabSizeBytes = 4096;
    cfg.MaxClassSize = 256;

    ::dng::core::SmallObjectAllocator allocator(&parent, cfg);

    constexpr std::size_t kSize = 48;
    constexpr std::size_t kAlign = alignof(std::max_align_t);

    void* block = allocator.Allocate(kSize, kAlign);
    DNG_CHECK(block != nullptr);

    allocator.Deallocate(block, kSize, kAlign);

    return 0;
}
