#pragma once
// ============================================================================
// D-Engine - Core/Memory/MemorySystem.hpp
// ----------------------------------------------------------------------------
// Purpose : Publish the lifecycle facade for the engine-wide memory subsystem.
//           MemorySystem exposes a concise static API that bootstraps global
//           allocators, registers subsystem arenas, and manages optional
//           thread-local allocators.
// Contract: Public header only declares the contract; mutable state and
//           initialization logic live in MemorySystem.cpp. Callers must invoke
//           Init() before consuming global allocators. Shutdown() is idempotent.
// Notes   : Designed as a thin facade per the handbook. Heavy allocator wiring,
//           environment override resolution, and logging stay out of the public
//           inclusion cone. A small RAII helper (MemorySystemScope) remains
//           inline for tests and short-lived tools.
// ============================================================================

#include "Core/Diagnostics/Check.hpp"
#include "Core/Memory/Allocator.hpp"
#include "Core/Memory/FrameAllocator.hpp"
#include "Core/Memory/MemoryConfig.hpp"

namespace dng
{
namespace memory
{
    using ::dng::core::AllocatorRef;
    using ::dng::core::MemoryConfig;

    struct MemorySystem
    {
        static void Init(const MemoryConfig& config = {}) noexcept;
        static void Shutdown() noexcept;
        static void OnThreadAttach() noexcept;
        static void OnThreadDetach() noexcept;
        [[nodiscard]] static bool TryGetActiveConfig(MemoryConfig& outConfig) noexcept;
        [[nodiscard]] static bool IsConfigCompatible(const MemoryConfig& config) noexcept;
        [[nodiscard]] static bool IsInitialized() noexcept;
        [[nodiscard]] static AllocatorRef GetDefaultAllocator() noexcept;
        [[nodiscard]] static AllocatorRef GetTrackingAllocator() noexcept;
        [[nodiscard]] static AllocatorRef GetSmallObjectAllocator() noexcept;
        [[nodiscard]] static ::dng::core::FrameAllocator& GetThreadFrameAllocator() noexcept;
    };

    class MemorySystemScope
    {
    public:
        explicit MemorySystemScope(const MemoryConfig& cfg = {})
            : mOwns(false)
        {
            const bool wasInitialized = MemorySystem::IsInitialized();
            MemorySystem::Init(cfg);
            mOwns = !wasInitialized;
        }

        MemorySystemScope(const MemorySystemScope&) = delete;
        MemorySystemScope& operator=(const MemorySystemScope&) = delete;

        MemorySystemScope(MemorySystemScope&& other) noexcept
            : mOwns(other.mOwns)
        {
            other.mOwns = false;
        }

        MemorySystemScope& operator=(MemorySystemScope&& other) noexcept
        {
            if (this != &other)
            {
                if (mOwns)
                {
                    MemorySystem::Shutdown();
                }
                mOwns = other.mOwns;
                other.mOwns = false;
            }
            return *this;
        }

        ~MemorySystemScope() noexcept
        {
            if (mOwns)
            {
                MemorySystem::Shutdown();
            }
        }

    private:
        bool mOwns;
    };

} // namespace memory
} // namespace dng

#ifndef DNG_MEMORY_INIT_GUARD
#define DNG_MEMORY_INIT_GUARD() \
    DNG_ASSERT(::dng::memory::MemorySystem::IsInitialized(), "MemorySystem must be initialized before use")
#endif
