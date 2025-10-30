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
#include "Core/Memory/MemMacros.hpp"
#include "Core/Memory/MemoryConfig.hpp"
#include "Core/Memory/DefaultAllocator.hpp"
#include "Core/Memory/TrackingAllocator.hpp"
#include "Core/Memory/GuardAllocator.hpp"
#include "Core/Memory/SmallObjectAllocator.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/ThreadSafety.hpp"
#include "Core/Memory/OOM.hpp"

#include <memory> // Needed for std::destroy_at (see usage on lines 152, 159, 166, etc.)
#include <new> // placement new / destroy_at
#include <cstdlib>
#include <limits>
#include <cstdint>
#include <cerrno>
#include <cstddef>

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

        enum class OverrideSource : std::uint8_t
        {
            Macro,
            Environment,
            Api
        };

        [[nodiscard]] constexpr const char* ToString(OverrideSource source) noexcept
        {
            switch (source)
            {
            case OverrideSource::Macro:       return "macro";
            case OverrideSource::Environment: return "env";
            case OverrideSource::Api:         return "api";
            default:                           return "unknown";
            }
        }

        struct OverrideResult
        {
            std::uint32_t value{ 0 };
            OverrideSource source{ OverrideSource::Macro };
            bool envInvalid{ false };
            bool apiInvalid{ false };
            bool clamped{ false };
            std::uint32_t envRaw{ 0 };
            std::uint32_t apiRaw{ 0 };
        };

        [[nodiscard]] inline bool TryParseU32(const char* text,
            std::uint32_t minValue,
            std::uint32_t maxValue,
            std::uint32_t& out) noexcept
        {
            if (!text || *text == '\0')
            {
                return false;
            }

            errno = 0;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(text, &end, 10);
            if ((errno != 0) || (end == text) || (*end != '\0'))
            {
                return false;
            }

            if (parsed < minValue || parsed > maxValue)
            {
                return false;
            }

            out = static_cast<std::uint32_t>(parsed);
            return true;
        }

        static constexpr const char* kEnvTrackingSampling = "DNG_MEM_TRACKING_SAMPLING_RATE";
        static constexpr const char* kEnvTrackingShards   = "DNG_MEM_TRACKING_SHARDS";
        static constexpr const char* kEnvSmallObjectBatch = "DNG_SOALLOC_BATCH";

    // Purpose : Retrieve environment variable without surfacing MSVC C4996 deprecation as an error.
    // Contract: Returns pointer owned by C runtime; do not free; thread-unsafe per C standard; name must be null-terminated.
    // Notes   : Header-safe wrapper to keep this file warning-clean on MSVC without globally defining _CRT_SECURE_NO_WARNINGS.
    [[nodiscard]] inline const char* GetEnvNoWarn(const char* name) noexcept
    {
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable:4996)
#endif
        return std::getenv(name);
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
    }

        [[nodiscard]] inline OverrideResult ResolveTrackingSampling(const MemoryConfig& cfg) noexcept
        {
            OverrideResult result{};
            result.value = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SAMPLING_RATE);
            result.source = OverrideSource::Macro;

            const char* envText = GetEnvNoWarn(kEnvTrackingSampling);
            if (envText && *envText)
            {
                std::uint32_t parsed = 0;
                if (TryParseU32(envText, 1u, std::numeric_limits<std::uint32_t>::max(), parsed))
                {
                    result.value = parsed;
                    result.source = OverrideSource::Environment;
                    result.envRaw = parsed;
                }
                else
                {
                    result.envInvalid = true;
                }
            }

            if (cfg.tracking_sampling_rate != 0u)
            {
                result.apiRaw = cfg.tracking_sampling_rate;
                if (cfg.tracking_sampling_rate >= 1u)
                {
                    result.value = cfg.tracking_sampling_rate;
                    result.source = OverrideSource::Api;
                }
                else
                {
                    result.apiInvalid = true;
                }
            }

            return result;
        }

        [[nodiscard]] inline OverrideResult ResolveTrackingShards(const MemoryConfig& cfg) noexcept
        {
            OverrideResult result{};
            result.value = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);
            result.source = OverrideSource::Macro;

            const char* envText = GetEnvNoWarn(kEnvTrackingShards);
            if (envText && *envText)
            {
                std::uint32_t parsed = 0;
                if (TryParseU32(envText, 1u, std::numeric_limits<std::uint32_t>::max(), parsed))
                {
                    result.envRaw = parsed;
                    if (::dng::core::IsPowerOfTwo(parsed))
                    {
                        result.value = parsed;
                        result.source = OverrideSource::Environment;
                    }
                    else
                    {
                        result.envInvalid = true;
                    }
                }
                else
                {
                    result.envInvalid = true;
                }
            }

            if (cfg.tracking_shard_count != 0u)
            {
                result.apiRaw = cfg.tracking_shard_count;
                if (::dng::core::IsPowerOfTwo(cfg.tracking_shard_count))
                {
                    result.value = cfg.tracking_shard_count;
                    result.source = OverrideSource::Api;
                }
                else
                {
                    result.apiInvalid = true;
                }
            }

            if (!::dng::core::IsPowerOfTwo(result.value))
            {
                result.clamped = true;
                result.value = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);
                result.source = OverrideSource::Macro;
            }

            return result;
        }

        [[nodiscard]] inline OverrideResult ResolveSmallObjectBatch(const MemoryConfig& cfg) noexcept
        {
            OverrideResult result{};
            result.value = static_cast<std::uint32_t>(DNG_SOALLOC_BATCH);
            result.source = OverrideSource::Macro;

            constexpr std::uint32_t kMaxBatch = static_cast<std::uint32_t>(DNG_SOA_TLS_MAG_CAPACITY);

            const char* envText = GetEnvNoWarn(kEnvSmallObjectBatch);
            if (envText && *envText)
            {
                std::uint32_t parsed = 0;
                if (TryParseU32(envText, 1u, std::numeric_limits<std::uint32_t>::max(), parsed))
                {
                    result.envRaw = parsed;
                    std::uint32_t sanitized = parsed;
                    if (sanitized < 1u)
                    {
                        sanitized = 1u;
                        result.clamped = true;
                    }
                    if (sanitized > kMaxBatch)
                    {
                        sanitized = kMaxBatch;
                        result.clamped = true;
                    }
                    result.value = sanitized;
                    result.source = OverrideSource::Environment;
                }
                else
                {
                    result.envInvalid = true;
                }
            }

            if (cfg.small_object_batch != 0u)
            {
                result.apiRaw = cfg.small_object_batch;
                if (cfg.small_object_batch >= 1u)
                {
                    std::uint32_t sanitized = cfg.small_object_batch;
                    if (sanitized > kMaxBatch)
                    {
                        sanitized = kMaxBatch;
                        result.clamped = true;
                    }
                    result.value = sanitized;
                    result.source = OverrideSource::Api;
                }
                else
                {
                    result.apiInvalid = true;
                }
            }

            if (result.value > kMaxBatch)
            {
                result.value = kMaxBatch;
                result.clamped = true;
            }

            return result;
        }

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

            auto& globalConfig = ::dng::core::MemoryConfig::GetGlobal();
            globalConfig = config;
            globals.activeConfig = globalConfig;

            const auto sampling = detail::ResolveTrackingSampling(globals.activeConfig);
            const auto shards   = detail::ResolveTrackingShards(globals.activeConfig);
            const auto batch    = detail::ResolveSmallObjectBatch(globals.activeConfig);

            const bool warnEnabled = ::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory");
            constexpr std::uint32_t kMaxSmallBatch = static_cast<std::uint32_t>(DNG_SOA_TLS_MAG_CAPACITY);

            std::uint32_t effectiveSampling = sampling.value;
            if (effectiveSampling == 0u)
            {
                effectiveSampling = 1u;
            }
            else if (effectiveSampling > 1u)
            {
                if (warnEnabled)
                {
                    DNG_LOG_WARNING("Memory",
                        "Tracking sampling rates >1 are not yet supported; falling back to 1 (requested {}).",
                        static_cast<unsigned long long>(effectiveSampling));
                }
                effectiveSampling = 1u;
            }

            std::uint32_t effectiveShards = shards.value;
            if (!::dng::core::IsPowerOfTwo(effectiveShards))
            {
                effectiveShards = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);
            }

            const std::uint32_t effectiveBatch = batch.value;

            const bool tlsBinsRequested = globalConfig.enable_smallobj_tls_bins;
            const bool tlsBinsCompiled  = (DNG_SMALLOBJ_TLS_BINS != 0);
            const bool tlsBinsEffective = tlsBinsCompiled && tlsBinsRequested;

            // Truth table (CT = DNG_SMALLOBJ_TLS_BINS, RT = enable_smallobj_tls_bins):
            // CT RT | Effective
            //  0  x | false (feature compiled out)
            //  1  0 | false (runtime opts out)
            //  1  1 | true  (TLS bins enabled)

            globalConfig.tracking_sampling_rate = effectiveSampling;
            globalConfig.tracking_shard_count   = effectiveShards;
            globalConfig.small_object_batch     = effectiveBatch;
            globalConfig.enable_smallobj_tls_bins = tlsBinsEffective;
            globals.activeConfig = globalConfig;

            if (warnEnabled)
            {
                if (sampling.envInvalid)
                {
                    DNG_LOG_WARNING("Memory", "Ignoring DNG_MEM_TRACKING_SAMPLING_RATE environment override (must be >= 1).");
                }
                if (sampling.apiInvalid)
                {
                    DNG_LOG_WARNING("Memory",
                        "Ignoring MemoryConfig::tracking_sampling_rate override {} (must be >= 1).",
                        static_cast<unsigned long long>(sampling.apiRaw));
                }

                if (shards.envInvalid)
                {
                    if (shards.envRaw != 0u)
                    {
                        DNG_LOG_WARNING("Memory",
                            "Ignoring DNG_MEM_TRACKING_SHARDS environment override {} (must be power-of-two).",
                            static_cast<unsigned long long>(shards.envRaw));
                    }
                    else
                    {
                        DNG_LOG_WARNING("Memory", "Ignoring DNG_MEM_TRACKING_SHARDS environment override (must be power-of-two).");
                    }
                }
                if (shards.apiInvalid)
                {
                    DNG_LOG_WARNING("Memory",
                        "Ignoring MemoryConfig::tracking_shard_count override {} (must be power-of-two).",
                        static_cast<unsigned long long>(shards.apiRaw));
                }
                if (shards.clamped && !shards.envInvalid && !shards.apiInvalid)
                {
                    DNG_LOG_WARNING("Memory",
                        "Tracking shard count fell back to compile-time default {} (invalid override).",
                        static_cast<unsigned long long>(static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS)));
                }

                if (batch.envInvalid)
                {
                    DNG_LOG_WARNING("Memory", "Ignoring DNG_SOALLOC_BATCH environment override (must be >= 1).");
                }
                if (batch.apiInvalid)
                {
                    DNG_LOG_WARNING("Memory",
                        "Ignoring MemoryConfig::small_object_batch override {} (must be >= 1).",
                        static_cast<unsigned long long>(batch.apiRaw));
                }
                if (batch.clamped)
                {
                    switch (batch.source)
                    {
                    case detail::OverrideSource::Environment:
                        DNG_LOG_WARNING("Memory",
                            "Clamped DNG_SOALLOC_BATCH override {} to {} (max capacity {}).",
                            static_cast<unsigned long long>(batch.envRaw),
                            static_cast<unsigned long long>(batch.value),
                            static_cast<unsigned long long>(kMaxSmallBatch));
                        break;
                    case detail::OverrideSource::Api:
                        DNG_LOG_WARNING("Memory",
                            "Clamped MemoryConfig::small_object_batch override {} to {} (max capacity {}).",
                            static_cast<unsigned long long>(batch.apiRaw),
                            static_cast<unsigned long long>(batch.value),
                            static_cast<unsigned long long>(kMaxSmallBatch));
                        break;
                    default:
                        DNG_LOG_WARNING("Memory",
                            "SmallObject batch default exceeded capacity; clamped to {}.",
                            static_cast<unsigned long long>(batch.value));
                        break;
                    }
                }
#if !DNG_SMALLOBJ_TLS_BINS
                if (tlsBinsRequested)
                {
                    DNG_LOG_WARNING("Memory",
                        "Ignoring MemoryConfig::enable_smallobj_tls_bins request (DNG_SMALLOBJ_TLS_BINS=0).");
                }
#endif
            }

            globals.defaultAllocator = new (globals.defaultAllocatorStorage) detail::DefaultAllocator();

            const std::uint32_t trackingSamplingRate = globals.activeConfig.tracking_sampling_rate != 0u
                ? globals.activeConfig.tracking_sampling_rate
                : static_cast<std::uint32_t>(DNG_MEM_TRACKING_SAMPLING_RATE);
            const std::uint32_t trackingShardCount = (globals.activeConfig.tracking_shard_count != 0u && ::dng::core::IsPowerOfTwo(globals.activeConfig.tracking_shard_count))
                ? globals.activeConfig.tracking_shard_count
                : static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);

            globals.trackingAllocator = new (globals.trackingAllocatorStorage)
                detail::TrackingAllocator(globals.defaultAllocator, trackingSamplingRate, trackingShardCount);
#if DNG_MEM_GUARDS
            globals.guardAllocator = new (globals.guardAllocatorStorage) detail::GuardAllocator(globals.trackingAllocator);
            auto* effectiveParent = static_cast<::dng::core::IAllocator*>(globals.guardAllocator);
#else
            auto* effectiveParent = static_cast<::dng::core::IAllocator*>(globals.trackingAllocator);
#endif

            ::dng::core::SmallObjectConfig smallCfg{};
            smallCfg.ReturnNullOnOOM = !globals.activeConfig.fatal_on_oom;
            smallCfg.TLSBatchSize = static_cast<std::size_t>(globals.activeConfig.small_object_batch);
            smallCfg.EnableTLSBins = globals.activeConfig.enable_smallobj_tls_bins;
            globals.smallObjectAllocator = new (globals.smallObjectStorage) detail::SmallObjectAllocator(effectiveParent, smallCfg);
            // Future hook: platform thread-detach callback should invoke globals.smallObjectAllocator->OnThreadExit().

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
                DNG_LOG_INFO("Memory",
                    "Tracking sampling rate={} (source={})",
                    static_cast<unsigned long long>(globals.activeConfig.tracking_sampling_rate),
                    detail::ToString(sampling.source));
                DNG_LOG_INFO("Memory",
                    "Tracking shard count={} (source={})",
                    static_cast<unsigned long long>(globals.activeConfig.tracking_shard_count),
                    detail::ToString(shards.source));
                DNG_LOG_INFO("Memory",
                    "SmallObject TLS batch={} (source={})",
                    static_cast<unsigned long long>(globals.activeConfig.small_object_batch),
                    detail::ToString(batch.source));
                DNG_LOG_INFO("Memory",
                    "SMALLOBJ_TLS_BINS: CT={} RT={} EFFECTIVE={}",
                    DNG_SMALLOBJ_TLS_BINS ? "1" : "0",
                    tlsBinsRequested ? "1" : "0",
                    globals.activeConfig.enable_smallobj_tls_bins ? "1" : "0");
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
