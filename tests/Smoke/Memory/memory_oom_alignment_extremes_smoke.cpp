// ============================================================================
// Memory OOM + Alignment Extremes Smoke Test
// ----------------------------------------------------------------------------
// Purpose : Validate explicit OOM paths and extreme alignment handling across
//           key memory allocators without relying on exceptions.
// Contract: Returns non-zero on failure; deterministic; no exceptions.
// ============================================================================

#if __has_include("Core/Memory/Allocator.hpp")
#    include "Core/Memory/Allocator.hpp"
#else
#    include "../../Source/Core/Memory/Allocator.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#if __has_include("Core/Memory/PageAllocator.hpp")
#    include "Core/Memory/PageAllocator.hpp"
#else
#    include "../../Source/Core/Memory/PageAllocator.hpp"
#endif

#if __has_include("Core/Memory/PoolAllocator.hpp")
#    include "Core/Memory/PoolAllocator.hpp"
#else
#    include "../../Source/Core/Memory/PoolAllocator.hpp"
#endif

#if __has_include("Core/Memory/SmallObjectAllocator.hpp")
#    include "Core/Memory/SmallObjectAllocator.hpp"
#else
#    include "../../Source/Core/Memory/SmallObjectAllocator.hpp"
#endif

#if __has_include("Core/Memory/TrackingAllocator.hpp")
#    include "Core/Memory/TrackingAllocator.hpp"
#else
#    include "../../Source/Core/Memory/TrackingAllocator.hpp"
#endif

#include <cstddef>
#include <cstdint>

namespace
{
    class NullAllocator final : public ::dng::core::IAllocator
    {
    public:
        [[nodiscard]] void* Allocate(::dng::core::usize, ::dng::core::usize) noexcept override
        {
            return nullptr;
        }

        void Deallocate(void*, ::dng::core::usize, ::dng::core::usize) noexcept override
        {
        }
    };
} // namespace

int RunMemoryOOMAlignmentExtremesSmoke()
{
    // OOM path: parent always returns nullptr and allocator must surface null.
    NullAllocator nullParent{};
    ::dng::core::SmallObjectConfig oomCfg{};
    oomCfg.ReturnNullOnOOM = true;
    oomCfg.EnableTLSBins = false;
    oomCfg.SlabSizeBytes = 4096u;
    oomCfg.MaxClassSize = 256u;
    {
        ::dng::core::SmallObjectAllocator allocator(&nullParent, oomCfg);
        if (allocator.Allocate(32u, alignof(std::max_align_t)) != nullptr)
        {
            return 1;
        }
    }

    // Alignment edge cases and pool exhaustion.
    ::dng::core::DefaultAllocator parent{};
    ::dng::core::TrackingAllocator tracking(&parent);

    ::dng::core::SmallObjectConfig cfg{};
    cfg.EnableTLSBins = false;
    cfg.SlabSizeBytes = 64u * 1024u;
    cfg.MaxClassSize = 256u;
    cfg.ReturnNullOnOOM = true;
    {
        ::dng::core::SmallObjectAllocator allocator(&tracking, cfg);

        constexpr std::size_t kSize = 64u;
        constexpr std::size_t kHighAlign = 256u;
        void* highAlign = allocator.Allocate(kSize, kHighAlign);
        if (!highAlign)
        {
            return 2;
        }
        if ((reinterpret_cast<std::uintptr_t>(highAlign) % kHighAlign) != 0u)
        {
            allocator.Deallocate(highAlign, kSize, kHighAlign);
            return 3;
        }
        allocator.Deallocate(highAlign, kSize, kHighAlign);

        void* zeroAlign = allocator.Allocate(kSize, 0u);
        if (!zeroAlign)
        {
            return 4;
        }
        allocator.Deallocate(zeroAlign, kSize, 0u);
    }

#if DNG_MEM_TRACKING
    if (tracking.GetActiveAllocationCount() != 0u)
    {
        return 5;
    }
#endif

    constexpr ::dng::core::usize kPoolBlockSize = 64u;
    constexpr ::dng::core::usize kPoolBlockAlign = alignof(std::max_align_t);
    ::dng::core::PoolAllocator pool(&parent, kPoolBlockSize, kPoolBlockAlign, 2u);

    void* p0 = pool.Allocate(kPoolBlockSize, kPoolBlockAlign);
    void* p1 = pool.Allocate(kPoolBlockSize, kPoolBlockAlign);
    void* p2 = pool.Allocate(kPoolBlockSize, kPoolBlockAlign);
    if (!p0 || !p1)
    {
        return 6;
    }
    if (p2 != nullptr)
    {
        return 7;
    }
    pool.Deallocate(p0, kPoolBlockSize, kPoolBlockAlign);
    pool.Deallocate(p1, kPoolBlockSize, kPoolBlockAlign);

    const std::size_t pageSize = ::dng::memory::PageSize();
    if (pageSize == 0u)
    {
        return 8;
    }
    void* region = ::dng::memory::Reserve(pageSize);
    if (!region)
    {
        return 9;
    }
    if ((reinterpret_cast<std::uintptr_t>(region) % pageSize) != 0u)
    {
        ::dng::memory::Release(region, pageSize);
        return 10;
    }
    ::dng::memory::Commit(region, pageSize);
    auto* bytes = static_cast<std::uint8_t*>(region);
    bytes[0] = 0x11u;
    bytes[pageSize - 1u] = 0x77u;
    ::dng::memory::Decommit(region, pageSize);
    ::dng::memory::Release(region, pageSize);

    return 0;
}
