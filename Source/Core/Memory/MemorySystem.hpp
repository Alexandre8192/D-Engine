#pragma once
// ============================================================================
// D-Engine - MemorySystem.hpp
// ----------------------------------------------------------------------------
// Purpose : Publish the lifecycle façade for the engine-wide memory subsystem.
//           MemorySystem exposes a concise static API that bootstraps all global
//           allocators, registers per-subsystem arenas, and manages optional
//           thread-local allocators when thread-safety is enabled.
// Contract: Header-only (no accompanying .cpp) and free of uncontrolled global
//           state. All mutable data lives inside the internal MemoryGlobals
//           singleton, guarded according to DNG_MEM_THREAD_POLICY. Clients must
//           call MemorySystem::Init() exactly once prior to using any global
//           allocator, and Shutdown() may be called multiple times (idempotent).
// Notes   : Designed for inclusion from CoreMinimal.hpp. No external runtime
//           dependencies are introduced and diagnostics leverage the existing
//           DNG_LOG_* and DNG_ASSERT/DNG_CHECK infrastructure. A convenience
//           RAII helper (MemorySystemScope) is offered for scope-based usage.
//           When DNG_MEM_GUARDS is enabled, all global allocators are wrapped
//           inside GuardAllocator to detect buffer overruns and use-after-free.
// ============================================================================

#include "Core/CoreMinimal.hpp"
#include "Core/Diagnostics/Check.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/DefaultAllocator.hpp"
#include "Core/Memory/TrackingAllocator.hpp"
#include "Core/Memory/GuardAllocator.hpp"
#include "Core/Memory/SmallObjectAllocator.hpp"
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/ThreadSafety.hpp"

#include <memory> // Needed for std::destroy_at (see usage on lines 152, 159, 166, etc.)
#include <new> // placement new / destroy_at

namespace dng { namespace core {
    class IAllocator;
    class DefaultAllocator;
    class TrackingAllocator;
    class SmallObjectAllocator;
    struct SmallObjectConfig;
    class ArenaAllocator;
    struct MemoryConfig;
    class AllocatorRef;
} }

namespace dng
{
namespace memory
{
    using ::dng::core::AllocatorRef;
    using ::dng::core::MemoryConfig;

    namespace detail
    {
        // ---------------------------------------------------------------------
        // Log category dedicated to MemorySystem lifecycle events.
        // ---------------------------------------------------------------------
        #ifndef DNG_MEMORY_SYSTEM_LOG_CATEGORY
        #define DNG_MEMORY_SYSTEM_LOG_CATEGORY "Memory.System"
        #endif

        // Forward aliases for brevity.
    using DefaultAllocator    = ::dng::core::DefaultAllocator;
    using TrackingAllocator   = ::dng::core::TrackingAllocator;
    using GuardAllocator      = ::dng::memory::GuardAllocator;
    using SmallObjectAllocator= ::dng::core::SmallObjectAllocator;
    using SmallObjectConfig   = ::dng::core::SmallObjectConfig;
    using ArenaAllocator      = ::dng::core::ArenaAllocator;
    using MemoryConfig        = ::dng::core::MemoryConfig;
    using AllocatorRef        = ::dng::core::AllocatorRef;
    using ThreadPolicy        = ::dng::core::DefaultThreadPolicy;
        using ThreadLock = typename ThreadPolicy::Lock;
        using ThreadMutex = typename ThreadPolicy::Mutex;

        // Arena sizing policy (may evolve alongside project needs).
    static constexpr std::size_t kRendererArenaBytes = 16u * 1024u * 1024u; // 16 MiB
    static constexpr std::size_t kAudioArenaBytes    =  8u * 1024u * 1024u; //  8 MiB
    static constexpr std::size_t kGameplayArenaBytes =  8u * 1024u * 1024u; //  8 MiB

        // ---------------------------------------------------------------------
        // MemoryGlobals
        // ---------------------------------------------------------------------
        // Purpose : Centralise all mutable memory-system state behind a single
        //           struct so we can reason about initialization / teardown.
        // Contract: Singleton storage (function-local static) and guarded with
        //           ThreadPolicy. Construction happens lazily at Init().
        // Notes   : Storage for concrete allocators is provided via raw byte
        //           buffers so we can orchestrate lifetime with placement new.
        // ---------------------------------------------------------------------
        struct MemoryGlobals
        {
            bool initialized{ false };
            MemoryConfig activeConfig{};

            DefaultAllocator*       defaultAllocator{ nullptr };
            TrackingAllocator*      trackingAllocator{ nullptr };
            GuardAllocator*         guardAllocator{ nullptr };
            SmallObjectAllocator*   smallObjectAllocator{ nullptr };
            ArenaAllocator*         rendererArena{ nullptr };
            ArenaAllocator*         audioArena{ nullptr };
            ArenaAllocator*         gameplayArena{ nullptr };

            ThreadMutex mutex{};
            std::size_t attachedThreads{ 0 };

            alignas(DefaultAllocator)       unsigned char defaultAllocatorStorage[sizeof(DefaultAllocator)]{};
            alignas(TrackingAllocator)      unsigned char trackingAllocatorStorage[sizeof(TrackingAllocator)]{};
            alignas(GuardAllocator)         unsigned char guardAllocatorStorage[sizeof(GuardAllocator)]{};
            alignas(SmallObjectAllocator)   unsigned char smallObjectStorage[sizeof(SmallObjectAllocator)]{};
            alignas(ArenaAllocator)         unsigned char rendererArenaStorage[sizeof(ArenaAllocator)]{};
            alignas(ArenaAllocator)         unsigned char audioArenaStorage[sizeof(ArenaAllocator)]{};
            alignas(ArenaAllocator)         unsigned char gameplayArenaStorage[sizeof(ArenaAllocator)]{};
        };

        [[nodiscard]] inline MemoryGlobals& Globals() noexcept
        {
            static MemoryGlobals g{};
            return g;
        }

        struct ThreadLocalState
        {
            AllocatorRef smallObject;
            bool attached{ false };
        };

        inline thread_local ThreadLocalState gThreadLocalState{};

        [[nodiscard]] inline AllocatorRef MakeAllocatorRef(DefaultAllocator* alloc) noexcept
        {
            return AllocatorRef(static_cast<::dng::core::IAllocator*>(alloc));
        }

        [[nodiscard]] inline AllocatorRef MakeAllocatorRef(TrackingAllocator* alloc) noexcept
        {
            return AllocatorRef(static_cast<::dng::core::IAllocator*>(alloc));
        }

        [[nodiscard]] inline AllocatorRef MakeAllocatorRef(SmallObjectAllocator* alloc) noexcept
        {
            return AllocatorRef(static_cast<::dng::core::IAllocator*>(alloc));
        }
        
        inline void DestroyGlobals(MemoryGlobals& globals) noexcept
        {
            if (globals.gameplayArena)
            {
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY,
                    "DestroyGlobals: destroying gameplay arena (ptr={}, capacity={}, valid={})",
                    static_cast<const void*>(globals.gameplayArena),
                    static_cast<unsigned long long>(globals.gameplayArena->GetCapacity()),
                    globals.gameplayArena->IsValid() ? "true" : "false");
                std::destroy_at(globals.gameplayArena);
                globals.gameplayArena = nullptr;
            }

            if (globals.audioArena)
            {
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: destroying audio arena");
                std::destroy_at(globals.audioArena);
                globals.audioArena = nullptr;
            }

            if (globals.rendererArena)
            {
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: destroying renderer arena");
                std::destroy_at(globals.rendererArena);
                globals.rendererArena = nullptr;
            }

            if (globals.smallObjectAllocator)
            {
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: destroying small object allocator");
                std::destroy_at(globals.smallObjectAllocator);
                globals.smallObjectAllocator = nullptr;
            }

            if (globals.guardAllocator)
            {
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: destroying guard allocator");
                std::destroy_at(globals.guardAllocator);
                globals.guardAllocator = nullptr;
            }

            if (globals.trackingAllocator)
            {
            #if DNG_MEM_TRACKING
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: reporting leaks");
                globals.trackingAllocator->ReportLeaks();
            #endif
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: destroying tracking allocator");
                std::destroy_at(globals.trackingAllocator);
                globals.trackingAllocator = nullptr;
            }

            if (globals.defaultAllocator)
            {
                DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "DestroyGlobals: destroying default allocator");
                std::destroy_at(globals.defaultAllocator);
                globals.defaultAllocator = nullptr;
            }

            globals.activeConfig = MemoryConfig{};
            globals.initialized = false;
        }

        inline void AttachThreadStateUnlocked(MemoryGlobals& globals) noexcept
        {
            if (gThreadLocalState.attached)
            {
                return;
            }

            gThreadLocalState.smallObject = MakeAllocatorRef(globals.smallObjectAllocator);
            gThreadLocalState.attached = true;

#if DNG_MEM_THREAD_SAFE
            ++globals.attachedThreads;
#endif
        }

        inline void DetachThreadStateUnlocked(MemoryGlobals& globals) noexcept
        {
            if (!gThreadLocalState.attached)
            {
                return;
            }

#if DNG_MEM_THREAD_SAFE
            if (globals.attachedThreads > 0)
            {
                --globals.attachedThreads;
            }
#endif
            gThreadLocalState.smallObject = AllocatorRef{};
            gThreadLocalState.attached = false;
        }

    } // namespace detail

    // -------------------------------------------------------------------------
    // MemorySystem
    // -------------------------------------------------------------------------
    // Purpose : Front-door static interface for memory subsystem lifecycle.
    // Contract: Clients must call Init() prior to using shared allocators.
    //           Shutdown() is idempotent and safe to call even if Init() failed.
    // Notes   : Implementation delegates to detail::MemoryGlobals.
    // -------------------------------------------------------------------------
    struct MemorySystem
    {
        static void Init(const MemoryConfig& config = {})
        {
            auto& globals = detail::Globals();
            detail::ThreadLock lock(globals.mutex);

            if (globals.initialized)
            {
                DNG_LOG_WARNING(DNG_MEMORY_SYSTEM_LOG_CATEGORY, "MemorySystem::Init() called twice; ignoring.");
                return;
            }

            ::dng::core::MemoryConfig::GetGlobal() = config;
            globals.activeConfig = ::dng::core::MemoryConfig::GetGlobal();

            globals.defaultAllocator = new (globals.defaultAllocatorStorage) detail::DefaultAllocator();
            globals.trackingAllocator = new (globals.trackingAllocatorStorage) detail::TrackingAllocator(globals.defaultAllocator);
#if DNG_MEM_GUARDS
            globals.guardAllocator = new (globals.guardAllocatorStorage) detail::GuardAllocator(globals.trackingAllocator);
            auto* effectiveParent = static_cast<::dng::core::IAllocator*>(globals.guardAllocator);
#else
            auto* effectiveParent = static_cast<::dng::core::IAllocator*>(globals.trackingAllocator);
#endif

            ::dng::core::SmallObjectConfig smallCfg{};
            smallCfg.ReturnNullOnOOM = !globals.activeConfig.fatal_on_oom;
            globals.smallObjectAllocator = new (globals.smallObjectStorage) detail::SmallObjectAllocator(effectiveParent, smallCfg);

            globals.rendererArena = new (globals.rendererArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kRendererArenaBytes);
            globals.audioArena    = new (globals.audioArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kAudioArenaBytes);
            globals.gameplayArena = new (globals.gameplayArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kGameplayArenaBytes);

            globals.initialized = true;

            DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY,
                "MemorySystem initialized (Tracking={}, ThreadSafe={})",
                globals.activeConfig.enable_tracking ? "true" : "false",
                globals.activeConfig.global_thread_safe ? "true" : "false");
            constexpr const char* kGuardState =
#if DNG_MEM_GUARDS
                "ENABLED";
#else
                "DISABLED";
#endif

            DNG_LOG_INFO(DNG_MEMORY_SYSTEM_LOG_CATEGORY,
                "MemorySystem: GuardAllocator {}",
                kGuardState);

            detail::AttachThreadStateUnlocked(globals);
        }

        static void Shutdown() noexcept
        {
            auto& globals = detail::Globals();
            detail::ThreadLock lock(globals.mutex);

            if (!globals.initialized)
            {
                return;
            }

            detail::DetachThreadStateUnlocked(globals);

    #if DNG_MEM_THREAD_SAFE
            if (globals.attachedThreads != 0)
            {
                DNG_LOG_WARNING(DNG_MEMORY_SYSTEM_LOG_CATEGORY,
                    "MemorySystem::Shutdown() detected {} threads still attached.",
                    static_cast<unsigned long long>(globals.attachedThreads));
            }
    #endif

            detail::DestroyGlobals(globals);
            ::dng::core::MemoryConfig::GetGlobal() = MemoryConfig{};
        }

        static void OnThreadAttach() noexcept
        {
            auto& globals = detail::Globals();
            detail::ThreadLock lock(globals.mutex);

            if (!globals.initialized)
            {
                DNG_LOG_WARNING(DNG_MEMORY_SYSTEM_LOG_CATEGORY,
                    "OnThreadAttach() ignored: MemorySystem not initialized.");
                return;
            }

            detail::AttachThreadStateUnlocked(globals);
        }

        static void OnThreadDetach() noexcept
        {
            auto& globals = detail::Globals();
            detail::ThreadLock lock(globals.mutex);

            if (!globals.initialized)
            {
                return;
            }

            detail::DetachThreadStateUnlocked(globals);
        }

        [[nodiscard]] static bool IsInitialized() noexcept
        {
            return detail::Globals().initialized;
        }

        [[nodiscard]] static AllocatorRef GetDefaultAllocator() noexcept
        {
            return detail::MakeAllocatorRef(detail::Globals().defaultAllocator);
        }

        [[nodiscard]] static AllocatorRef GetTrackingAllocator() noexcept
        {
            return detail::MakeAllocatorRef(detail::Globals().trackingAllocator);
        }

        [[nodiscard]] static AllocatorRef GetSmallObjectAllocator() noexcept
        {
            return detail::MakeAllocatorRef(detail::Globals().smallObjectAllocator);
        }
    };

    // -------------------------------------------------------------------------
    // MemorySystemScope
    // -------------------------------------------------------------------------
    // Purpose : RAII helper that guarantees Init()/Shutdown() pairing.
    // Contract: Multiple scopes may coexist; only the outermost scope calls
    //           Shutdown(). Construction accepts the same config as Init().
    // Notes   : Useful for tests or CLI tools.
    // -------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Macro helper for guarding memory-system usage.
// -----------------------------------------------------------------------------
#ifndef DNG_MEMORY_INIT_GUARD
#define DNG_MEMORY_INIT_GUARD() \
    DNG_ASSERT(::dng::memory::MemorySystem::IsInitialized(), "MemorySystem must be initialized before use")
#endif
