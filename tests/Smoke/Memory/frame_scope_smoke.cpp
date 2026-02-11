// ============================================================================
// FrameScope Smoke Test
// ----------------------------------------------------------------------------
// Ensures FrameScope.hpp is self-contained and validates scoped rewind behavior.
// ============================================================================

#if __has_include("Core/Memory/FrameScope.hpp")
#    include "Core/Memory/FrameScope.hpp"
#else
#    include "../../Source/Core/Memory/FrameScope.hpp"
#endif

int RunFrameScopeSmoke()
{
    ::dng::memory::MemorySystem::Shutdown();

    ::dng::core::MemoryConfig cfg{};
    cfg.SetThreadFrameAllocatorBytes(8u * 1024u);
    cfg.SetThreadFrameReturnNull(true);

    ::dng::memory::MemorySystem::Init(cfg);
    if (!::dng::memory::MemorySystem::IsInitialized())
    {
        return 1;
    }

    ::dng::core::FrameAllocator& frame = ::dng::memory::MemorySystem::GetThreadFrameAllocator();
    const ::dng::core::usize usedBefore = frame.GetUsed();

    {
        ::dng::memory::FrameScope frameScope{};
        void* block = frameScope.GetAllocator().Allocate(128u, 16u);
        if (!block)
        {
            ::dng::memory::MemorySystem::Shutdown();
            return 2;
        }

        if (frameScope.GetAllocator().GetUsed() <= usedBefore)
        {
            ::dng::memory::MemorySystem::Shutdown();
            return 3;
        }
    }

    if (frame.GetUsed() != usedBefore)
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 4;
    }

    ::dng::memory::MemorySystem::Shutdown();
    return 0;
}

#if 0
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
