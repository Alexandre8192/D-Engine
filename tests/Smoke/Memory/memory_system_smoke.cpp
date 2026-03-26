// ============================================================================
// Memory System Smoke Test
// ----------------------------------------------------------------------------
// Ensures MemorySystem public headers remain self-contained and validates
// init/shutdown plus basic allocator access.
// ============================================================================

#if __has_include("Core/Memory/MemorySystem.hpp")
#    include "Core/Memory/MemorySystem.hpp"
#else
#    include "../../Source/Core/Memory/MemorySystem.hpp"
#endif

#include <cstddef>

int RunMemorySystemSmoke()
{
    ::dng::memory::MemorySystem::Shutdown();

    ::dng::core::MemoryConfig cfg{};
    cfg.SetThreadFrameAllocatorBytes(4u * 1024u);
    cfg.SetThreadFrameReturnNull(true);

    ::dng::memory::MemorySystem::Init(cfg);
    if (!::dng::memory::MemorySystem::IsInitialized())
    {
        return 1;
    }

    ::dng::core::MemoryConfig activeConfig{};
    if (!::dng::memory::MemorySystem::TryGetActiveConfig(activeConfig))
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 6;
    }

    if (!::dng::memory::MemorySystem::IsConfigCompatible(cfg))
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 7;
    }

    ::dng::core::MemoryConfig conflictingConfig = activeConfig;
    conflictingConfig.SetThreadFrameAllocatorBytes(activeConfig.thread_frame_allocator_bytes + 64u);
    if (::dng::memory::MemorySystem::IsConfigCompatible(conflictingConfig))
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 8;
    }

    ::dng::core::AllocatorRef defaultAllocator = ::dng::memory::MemorySystem::GetDefaultAllocator();
    if (!defaultAllocator.IsValid())
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 2;
    }

    constexpr std::size_t kSize = 128u;
    constexpr std::size_t kAlign = alignof(std::max_align_t);
    void* block = defaultAllocator.AllocateBytes(kSize, kAlign);
    if (!block)
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 3;
    }
    defaultAllocator.DeallocateBytes(block, kSize, kAlign);

    ::dng::core::FrameAllocator& frame = ::dng::memory::MemorySystem::GetThreadFrameAllocator();
    const ::dng::core::FrameMarker marker = frame.GetMarker();
    void* frameBlock = frame.Allocate(64u, 16u);
    if (!frameBlock)
    {
        ::dng::memory::MemorySystem::Shutdown();
        return 4;
    }
    frame.Rewind(marker);

    ::dng::memory::MemorySystem::Shutdown();
    if (::dng::memory::MemorySystem::IsInitialized())
    {
        return 5;
    }

    // Verify that global_thread_policy is ignored when global_thread_safe is false.
    // Two configs that differ only in global_thread_policy while global_thread_safe==false
    // must be treated as compatible (the policy has no effect when thread-safety is off).
    {
        ::dng::core::MemoryConfig baseConfig{};
        baseConfig.global_thread_safe   = false;
        baseConfig.global_thread_policy = 0;
        ::dng::memory::MemorySystem::Init(baseConfig);
        if (!::dng::memory::MemorySystem::IsInitialized())
        {
            return 9;
        }

        ::dng::core::MemoryConfig altPolicyConfig{};
        altPolicyConfig.global_thread_safe   = false;
        altPolicyConfig.global_thread_policy = 1; // different policy, but thread-safety is off
        if (!::dng::memory::MemorySystem::IsConfigCompatible(altPolicyConfig))
        {
            ::dng::memory::MemorySystem::Shutdown();
            return 10;
        }

        ::dng::memory::MemorySystem::Shutdown();
    }

    return 0;
}
