// ============================================================================
// PageAllocator Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Validate reserve/commit/decommit/release lifecycle for the
//           virtual memory facade.
// Contract: No exceptions; deterministic; returns non-zero on failure.
// ============================================================================

#if __has_include("Core/Memory/PageAllocator.hpp")
#    include "Core/Memory/PageAllocator.hpp"
#else
#    include "../../Source/Core/Memory/PageAllocator.hpp"
#endif

#include <cstddef>
#include <cstdint>

int RunPageAllocatorSmoke()
{
    const std::size_t pageSize = ::dng::memory::PageSize();
    if (pageSize == 0u)
    {
        return 1;
    }

    void* region = ::dng::memory::Reserve(pageSize);
    if (!region)
    {
        return 2;
    }

    ::dng::memory::Commit(region, pageSize);

    auto* bytes = static_cast<std::uint8_t*>(region);
    bytes[0] = 0xA5u;
    bytes[pageSize - 1u] = 0x5Au;

    ::dng::memory::Decommit(region, pageSize);
    ::dng::memory::Release(region, pageSize);
    return 0;
}
