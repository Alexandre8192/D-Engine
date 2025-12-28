// ============================================================================
// FrameScope Smoke Test (compile-only)
// ----------------------------------------------------------------------------
// Ensures FrameScope.hpp is self-contained and demonstrates typical usage.
// ============================================================================

#if __has_include("Core/Memory/FrameScope.hpp")
#    include "Core/Memory/FrameScope.hpp"
#else
#    include "../../Source/Core/Memory/FrameScope.hpp"
#endif

#if 0
#    include "Core/Memory/MemoryConfig.hpp"
#    include "Core/Memory/MemorySystem.hpp"

namespace dng::tests
{
    inline void FrameScopeSmoke() noexcept
    {
        auto cfg = ::dng::core::MemoryConfig{};
        cfg.SetThreadFrameAllocatorBytes(4u * 1024u);
        cfg.SetThreadFrameReturnNull(true);

        ::dng::memory::MemorySystemScope systemScope{ cfg };

        {
            ::dng::memory::FrameScope frameScope{};
            void* block = frameScope.GetAllocator().Allocate(128u);
            (void)block;
        }
    }
}
#endif
