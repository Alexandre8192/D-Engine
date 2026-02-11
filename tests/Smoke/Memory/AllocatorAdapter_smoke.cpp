// ============================================================================
// Allocator Adapter Smoke Test (compile-only)
// ----------------------------------------------------------------------------
// Verifies the adapter header remains self-contained and STL-friendly.
// ============================================================================

#if __has_include("Core/Memory/AllocatorAdapter.hpp")
#    include "Core/Memory/AllocatorAdapter.hpp"
#else
#    include "../../Source/Core/Memory/AllocatorAdapter.hpp"
#endif

#if __has_include("Core/Memory/DefaultAllocator.hpp")
#    include "Core/Memory/DefaultAllocator.hpp"
#else
#    include "../../Source/Core/Memory/DefaultAllocator.hpp"
#endif

#include <type_traits>
#include <vector>

#if 0
#include "Core/Memory/MemorySystem.hpp"

namespace dng::tests
{
    inline void AllocatorAdapterSmoke() noexcept
    {
        using Adapter = dng::core::AllocatorAdapter<unsigned char>;
        static_assert(std::is_trivially_copyable_v<Adapter>, "Adapter must stay POD");

        Adapter adapter{};
        std::vector<unsigned char, Adapter> bytes(adapter);
        (void)bytes;
    }
}
#endif

using DngAllocatorAdapterVector = std::vector<unsigned char, dng::core::AllocatorAdapter<unsigned char>>;
static_assert(std::is_trivially_copyable_v<dng::core::AllocatorAdapter<unsigned char>>, "AllocatorAdapter should be trivially copyable");

int RunAllocatorAdapterSmoke()
{
    ::dng::core::DefaultAllocator parent{};
    ::dng::core::AllocatorRef ref(static_cast<::dng::core::IAllocator*>(&parent));
    ::dng::core::AllocatorAdapter<unsigned char> adapter(ref);

    DngAllocatorAdapterVector bytes(adapter);
    bytes.reserve(8);
    bytes.push_back(0x2A);
    bytes.push_back(0x7C);

    if (bytes.size() != 2u)
    {
        return 1;
    }

    if (bytes[0] != 0x2A || bytes[1] != 0x7C)
    {
        return 2;
    }

    return 0;
}
