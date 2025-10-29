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
//           Global OOM escalation funnels through GlobalNewDelete.cpp, the only
//           Core site permitted to emit std::bad_alloc.
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
#include "Core/Memory/OOM.hpp"

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
            const bool logInfo = ::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Info, "Memory");

            if (globals.gameplayArena)
            {
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory",
                        "DestroyGlobals: destroying gameplay arena (ptr={}, capacity={}, valid={})",
                        static_cast<const void*>(globals.gameplayArena),
                        static_cast<unsigned long long>(globals.gameplayArena->GetCapacity()),
                        globals.gameplayArena->IsValid() ? "true" : "false");
                }
                std::destroy_at(globals.gameplayArena);
                globals.gameplayArena = nullptr;
            }

            if (globals.audioArena)
            {
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying audio arena");
                }
                std::destroy_at(globals.audioArena);
                globals.audioArena = nullptr;
            }

            if (globals.rendererArena)
            {
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying renderer arena");
                }
                std::destroy_at(globals.rendererArena);
                globals.rendererArena = nullptr;
            }

            if (globals.smallObjectAllocator)
            {
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying small object allocator");
                }
                std::destroy_at(globals.smallObjectAllocator);
                globals.smallObjectAllocator = nullptr;
            }

            if (globals.guardAllocator)
            {
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying guard allocator");
                }
                std::destroy_at(globals.guardAllocator);
                globals.guardAllocator = nullptr;
            }

            if (globals.trackingAllocator)
            {
#if DNG_MEM_TRACKING
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: reporting leaks");
                }
                globals.trackingAllocator->ReportLeaks();
#endif
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying tracking allocator");
                }
                std::destroy_at(globals.trackingAllocator);
                globals.trackingAllocator = nullptr;
            }

            if (globals.defaultAllocator)
            {
                if (logInfo)
                {
                    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying default allocator");
                }
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

    // =====================================================================
    // Purpose : Expose a contracts-first façade over the engine-wide memory
    //           subsystem, wiring global allocators and per-thread contexts.
    // Contract: Callers must execute `Init()` before consuming any global
    //           allocator accessors. `Shutdown()` may be invoked multiple
    //           times but only tears down once. Public methods are thread-safe
    //           via the internal mutex; thread attach/detach helpers must be
    //           paired by the owning thread. No hidden allocations occur in
    //           this header; arena provisioning failures route through the
    //           central `DNG_MEM_CHECK_OOM` policy.
    // Notes   : Implementation delegates storage to detail::MemoryGlobals so
    //           the header remains free of static data. Compile-time toggles
    //           (tracking, guards, thread safety) gate optional subsystems.
    // =====================================================================
    struct MemorySystem
    {
        // Purpose : Bootstrap global allocators and attach the calling thread's small-object context.
        // Contract: Thread-safe; a second invocation while already initialized is ignored. Must precede allocator accessors.
        // Notes   : Copies the supplied config into global state and emits OOM diagnostics if arena provisioning fails.
        static void Init(const MemoryConfig& config = {})
        {
            auto& globals = detail::Globals();
            detail::ThreadLock lock(globals.mutex);

            if (globals.initialized)
            {
                if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory"))
                {
                    DNG_LOG_WARNING("Memory", "MemorySystem::Init() called twice; ignoring.");
                }
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
            if (!globals.rendererArena->IsValid())
            {
                DNG_MEM_CHECK_OOM(detail::kRendererArenaBytes, alignof(detail::ArenaAllocator), "MemorySystem::Init rendererArena");
            }
            globals.audioArena    = new (globals.audioArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kAudioArenaBytes);
            if (!globals.audioArena->IsValid())
            {
                DNG_MEM_CHECK_OOM(detail::kAudioArenaBytes, alignof(detail::ArenaAllocator), "MemorySystem::Init audioArena");
            }
            globals.gameplayArena = new (globals.gameplayArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kGameplayArenaBytes);
            if (!globals.gameplayArena->IsValid())
            {
                DNG_MEM_CHECK_OOM(detail::kGameplayArenaBytes, alignof(detail::ArenaAllocator), "MemorySystem::Init gameplayArena");
            }

            globals.initialized = true;

            const bool logInfo = ::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Info, "Memory");
            if (logInfo)
            {
                DNG_LOG_INFO("Memory",
                    "MemorySystem initialized (Tracking={}, ThreadSafe={})",
                    globals.activeConfig.enable_tracking ? "true" : "false",
                    globals.activeConfig.global_thread_safe ? "true" : "false");
            }
            constexpr const char* kGuardState =
#if DNG_MEM_GUARDS
                "ENABLED";
#else
                "DISABLED";
#endif

            if (logInfo)
            {
                DNG_LOG_INFO("Memory",
                    "MemorySystem: GuardAllocator {}",
                    kGuardState);
            }

            detail::AttachThreadStateUnlocked(globals);
        }

        // Purpose : Tear down all global allocators and detach thread-local state.
        // Contract: Thread-safe; safe to call even if initialization never succeeded. Idempotent across repeated calls.
        // Notes   : Warns when threads remain attached and always resets runtime MemoryConfig to defaults.
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
                if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory"))
                {
                    DNG_LOG_WARNING("Memory",
                        "MemorySystem::Shutdown() detected {} threads still attached.",
                        static_cast<unsigned long long>(globals.attachedThreads));
                }
            }
#endif

            detail::DestroyGlobals(globals);
            ::dng::core::MemoryConfig::GetGlobal() = MemoryConfig{};
        }

        // Purpose : Bind the calling thread to MemorySystem-managed thread-local allocators.
        // Contract: Thread-safe; must only be invoked after successful `Init()`. Safe to call redundantly per thread.
        // Notes   : Emits a warning when called before initialization to highlight misuse.
        static void OnThreadAttach() noexcept
        {
            auto& globals = detail::Globals();
            detail::ThreadLock lock(globals.mutex);

            if (!globals.initialized)
            {
                if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory"))
                {
                    DNG_LOG_WARNING("Memory",
                        "OnThreadAttach() ignored: MemorySystem not initialized.");
                }
                return;
            }

            detail::AttachThreadStateUnlocked(globals);
        }

        // Purpose : Release thread-local allocator bindings for the calling thread.
        // Contract: Thread-safe; no effect when MemorySystem is not initialized or the thread was never attached.
        // Notes   : Balanced with `OnThreadAttach`; reduces leak reporting noise in thread-safe builds.
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

        // Purpose : Report whether MemorySystem successfully completed initialization.
        // Contract: Lock-free; returns a snapshot suitable for guards but not a substitute for the mutex when mutating state.
        // Notes   : Used by helper scopes and guard macros to detect setup status.
        [[nodiscard]] static bool IsInitialized() noexcept
        {
            return detail::Globals().initialized;
        }

        // Purpose : Provide a façade over the default allocator wired during `Init()`.
        // Contract: Call only after initialization; returned reference is non-owning and may be invalid if MemorySystem is down.
        // Notes   : No hidden allocations; simply wraps the internal pointer.
        [[nodiscard]] static AllocatorRef GetDefaultAllocator() noexcept
        {
            return detail::MakeAllocatorRef(detail::Globals().defaultAllocator);
        }

        // Purpose : Expose the tracking allocator (if enabled) to subsystems needing diagnostics.
        // Contract: Requires prior `Init()`; returns an empty reference when tracking is compiled out.
        // Notes   : Permits callers to opt-in to leak tracking without owning the allocator instance.
        [[nodiscard]] static AllocatorRef GetTrackingAllocator() noexcept
        {
            return detail::MakeAllocatorRef(detail::Globals().trackingAllocator);
        }

        // Purpose : Surface the small-object allocator configured for hot-path allocations.
        // Contract: Requires MemorySystem initialization; reference becomes invalid after `Shutdown()`.
        // Notes   : Thread-safe builds lazily attach per-thread state via `OnThreadAttach()`.
        [[nodiscard]] static AllocatorRef GetSmallObjectAllocator() noexcept
        {
            return detail::MakeAllocatorRef(detail::Globals().smallObjectAllocator);
        }
    };

    // =====================================================================
    // Purpose : RAII helper that guarantees balanced MemorySystem Init/Shutdown
    //           during scoped usage (e.g., tests or command-line tools).
    // Contract: Constructing a scope triggers `Init()`; destruction triggers
    //           `Shutdown()` only if this scope performed the initialization.
    //           Move semantics transfer ownership; copy is disabled.
    // Notes   : Allows nested scopes so long as callers respect the outermost
    //           owner model; avoids hidden global state changes in headers.
    // =====================================================================
    class MemorySystemScope
    {
    public:
        // Purpose : Enter a temporary MemorySystem context with the provided configuration.
        // Contract: Thread-safe; reuses an existing initialization without reinitializing and records ownership for teardown.
        // Notes   : Typically used in tests to force deterministic allocator setup per case.
        explicit MemorySystemScope(const MemoryConfig& cfg = {})
            : mOwns(false)
        {
            const bool wasInitialized = MemorySystem::IsInitialized();
            MemorySystem::Init(cfg);
            mOwns = !wasInitialized;
        }

        MemorySystemScope(const MemorySystemScope&) = delete;
        MemorySystemScope& operator=(const MemorySystemScope&) = delete;

        // Purpose : Transfer scope ownership while preserving balanced shutdown semantics.
        // Contract: Source scope relinquishes responsibility; destination inherits ownership flag.
        // Notes   : Enables scope storage inside move-only containers.
        MemorySystemScope(MemorySystemScope&& other) noexcept
            : mOwns(other.mOwns)
        {
            other.mOwns = false;
        }

        // Purpose : Move-assign scope ownership, shutting down if the current scope is responsible.
        // Contract: Self-assignment safe; ensures prior ownership triggers Shutdown() before adopting the new flag.
        // Notes   : Leaves the source scope inactive.
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

        // Purpose : Ensure Shutdown() is invoked when this scope owns the global initialization.
        // Contract: Noexcept; safe when MemorySystem was already torn down elsewhere.
        // Notes   : Acts as the mirrored exit counterpart for the constructor behaviour.
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
// Purpose : Short assertion helper ensuring MemorySystem is live before use.
// Contract: Intended for debug-only call-site guards; expands to a DNG_ASSERT with a deterministic message.
// Notes   : Keeps headers free from silent initialization while still flagging misuse in development builds.
#define DNG_MEMORY_INIT_GUARD() \
    DNG_ASSERT(::dng::memory::MemorySystem::IsInitialized(), "MemorySystem must be initialized before use")
#endif
