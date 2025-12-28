#if __has_include("Core/Memory/MemorySystem.hpp")
#    include "Core/Memory/MemorySystem.hpp"
#else
#    include <cstddef>
#    include <new>
namespace dng { namespace memory {
    struct MemorySystemPlaceholder final
    {
        static void Init(std::size_t = 0, const std::nothrow_t* = nullptr) noexcept {}
        static void Shutdown() noexcept {}
    };
} }
#endif

#if 0
#include <new>

namespace dng::tests
{
    struct alignas(32) AlignedPod
    {
        unsigned char payload[64];
    };

    inline void NewDeleteSmoke() noexcept
    {
        auto* scalar = new AlignedPod{};
        delete scalar;

        auto* scalarAligned = new (std::align_val_t{32}) AlignedPod{};
        delete scalarAligned;

        auto* arrayDefault = new AlignedPod[4];
        delete[] arrayDefault;

        auto* arrayAligned = new (std::align_val_t{32}) AlignedPod[2];
        delete[] arrayAligned;

        auto* arrayAlignedNoThrow = new (std::align_val_t{16}, std::nothrow) AlignedPod[1];
        delete[] arrayAlignedNoThrow;

    #if defined(DNG_MEMORY_TEST_FORCE_OOM)
        void* forced = ::operator new(128, std::nothrow);
        (void)forced;
    #endif
    }
}
#endif
