// Compile-only + runtime smoke test to ensure GuardAllocator alignment uses canonical helpers
#include "Core/Diagnostics/Check.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/DefaultAllocator.hpp"
#include "Core/Memory/GuardAllocator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dng::tests
{
namespace
{
    void ValidateAligned(void* ptr, std::size_t requestedAlignment, std::size_t size, memory::GuardAllocator& alloc) noexcept
    {
        if (!ptr)
        {
            DNG_CHECK(false && "GuardAllocator returned nullptr in alignment smoke test");
            return;
        }

        const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);
        const core::usize normalized = core::NormalizeAlignment(requestedAlignment);
        DNG_CHECK(addr % normalized == 0 && "GuardAllocator pointer is not aligned to normalized boundary");

        alloc.Deallocate(ptr, size, requestedAlignment);
    }
}

[[maybe_unused]] void RunGuardAllocatorAlignmentSmoke() noexcept
{
    core::DefaultAllocator parent{};
    memory::GuardAllocator guard(&parent);

    constexpr std::array<std::size_t, 4> alignments{ 8u, 16u, 32u, 64u };
    constexpr std::size_t payloadSize = 128u;

    for (std::size_t alignment : alignments)
    {
        void* ptr = guard.Allocate(payloadSize, alignment);
        ValidateAligned(ptr, alignment, payloadSize, guard);
    }

    // Zero alignment should use NormalizeAlignment fallback.
    void* defaultAligned = guard.Allocate(payloadSize, 0u);
    ValidateAligned(defaultAligned, 0u, payloadSize, guard);
}

} // namespace dng::tests
