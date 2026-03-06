// ============================================================================
// D-Engine - Core/Memory/MemorySystem.cpp
// ----------------------------------------------------------------------------
// Purpose : Implement the engine-wide memory subsystem lifecycle facade.
// Contract: No exceptions/RTTI; all mutable state remains private to this TU;
//           public synchronization happens through MemorySystem's static API.
// Notes   : This file owns the singleton storage, init-time override
//           resolution, allocator construction/destruction, and per-thread
//           frame allocator plumbing that used to live in the public header.
// ============================================================================

#include "Core/Memory/MemorySystem.hpp"

#include "Core/Logger.hpp"
#include "Core/Memory/Alignment.hpp"
#include "Core/Memory/ArenaAllocator.hpp"
#include "Core/Memory/DefaultAllocator.hpp"
#include "Core/Memory/GuardAllocator.hpp"
#include "Core/Memory/OOM.hpp"
#include "Core/Memory/SmallObjectAllocator.hpp"
#include "Core/Memory/ThreadSafety.hpp"
#include "Core/Memory/TrackingAllocator.hpp"
#include "Core/Platform/PlatformCrt.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace dng
{
namespace memory
{
namespace detail
{
    using DefaultAllocator = ::dng::core::DefaultAllocator;
    using TrackingAllocator = ::dng::core::TrackingAllocator;
    using GuardAllocator = ::dng::memory::GuardAllocator;
    using SmallObjectAllocator = ::dng::core::SmallObjectAllocator;
    using ArenaAllocator = ::dng::core::ArenaAllocator;
    using MemoryConfig = ::dng::core::MemoryConfig;
    using AllocatorRef = ::dng::core::AllocatorRef;
    using FrameAllocator = ::dng::core::FrameAllocator;
    using FrameAllocatorConfig = ::dng::core::FrameAllocatorConfig;
    using ThreadPolicy = ::dng::core::DefaultThreadPolicy;
    using ThreadLock = typename ThreadPolicy::Lock;
    using ThreadMutex = typename ThreadPolicy::Mutex;

    static constexpr std::size_t kRendererArenaBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t kAudioArenaBytes = 8u * 1024u * 1024u;
    static constexpr std::size_t kGameplayArenaBytes = 8u * 1024u * 1024u;

    struct MemoryGlobals
    {
        bool initialized{ false };
        MemoryConfig activeConfig{};

        DefaultAllocator* defaultAllocator{ nullptr };
        TrackingAllocator* trackingAllocator{ nullptr };
        GuardAllocator* guardAllocator{ nullptr };
        SmallObjectAllocator* smallObjectAllocator{ nullptr };
        ArenaAllocator* rendererArena{ nullptr };
        ArenaAllocator* audioArena{ nullptr };
        ArenaAllocator* gameplayArena{ nullptr };

        FrameAllocatorConfig threadFrameConfig{};
        std::size_t threadFrameBytes{ 0 };

        ThreadMutex mutex{};
        std::size_t attachedThreads{ 0 };

        alignas(DefaultAllocator) unsigned char defaultAllocatorStorage[sizeof(DefaultAllocator)]{};
        alignas(TrackingAllocator) unsigned char trackingAllocatorStorage[sizeof(TrackingAllocator)]{};
        alignas(GuardAllocator) unsigned char guardAllocatorStorage[sizeof(GuardAllocator)]{};
        alignas(SmallObjectAllocator) unsigned char smallObjectStorage[sizeof(SmallObjectAllocator)]{};
        alignas(ArenaAllocator) unsigned char rendererArenaStorage[sizeof(ArenaAllocator)]{};
        alignas(ArenaAllocator) unsigned char audioArenaStorage[sizeof(ArenaAllocator)]{};
        alignas(ArenaAllocator) unsigned char gameplayArenaStorage[sizeof(ArenaAllocator)]{};
    };

    [[nodiscard]] MemoryGlobals& Globals() noexcept
    {
        static MemoryGlobals globals{};
        return globals;
    }

    struct ThreadLocalState
    {
        AllocatorRef smallObject;
        FrameAllocator* frameAllocator{ nullptr };
        void* frameBacking{ nullptr };
        std::size_t frameCapacity{ 0 };
        bool attached{ false };
        alignas(FrameAllocator) unsigned char frameStorage[sizeof(FrameAllocator)]{};
    };

    thread_local ThreadLocalState gThreadLocalState{};

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
        case OverrideSource::Macro: return "macro";
        case OverrideSource::Environment: return "env";
        case OverrideSource::Api: return "api";
        default: return "unknown";
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

    [[nodiscard]] bool TryParseU32(const char* text,
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
    static constexpr const char* kEnvTrackingShards = "DNG_MEM_TRACKING_SHARDS";
    static constexpr const char* kEnvSmallObjectBatch = "DNG_SOALLOC_BATCH";

    [[nodiscard]] OverrideResult ResolveTrackingSampling(const MemoryConfig& cfg) noexcept
    {
        OverrideResult result{};
        result.value = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SAMPLING_RATE);
        result.source = OverrideSource::Macro;

        const char* envText = platform::GetEnvNoWarn(kEnvTrackingSampling);
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

    [[nodiscard]] OverrideResult ResolveTrackingShards(const MemoryConfig& cfg) noexcept
    {
        OverrideResult result{};
        result.value = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);
        result.source = OverrideSource::Macro;

        const char* envText = platform::GetEnvNoWarn(kEnvTrackingShards);
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

    [[nodiscard]] OverrideResult ResolveSmallObjectBatch(const MemoryConfig& cfg) noexcept
    {
        OverrideResult result{};
        result.value = static_cast<std::uint32_t>(DNG_SOALLOC_BATCH);
        result.source = OverrideSource::Macro;

        constexpr std::uint32_t kMaxBatch = static_cast<std::uint32_t>(DNG_SOA_TLS_MAG_CAPACITY);

        const char* envText = platform::GetEnvNoWarn(kEnvSmallObjectBatch);
        if (envText && *envText)
        {
            std::uint32_t parsed = 0;
            if (TryParseU32(envText, 1u, std::numeric_limits<std::uint32_t>::max(), parsed))
            {
                result.envRaw = parsed;
                std::uint32_t sanitized = parsed;
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

    struct InitResolution
    {
        MemoryConfig effectiveConfig{};
        FrameAllocatorConfig threadFrameConfig{};
        std::size_t threadFrameBytes{ 0 };
        OverrideResult sampling{};
        OverrideResult shards{};
        OverrideResult batch{};
        bool trackingCompiled{ false };
        bool guardsCompiled{ false };
        bool tlsBinsCompiled{ false };
        bool trackingRequested{ false };
        bool guardsRequested{ false };
        bool tlsBinsRequested{ false };
    };

    [[nodiscard]] InitResolution ResolveInitConfig(const MemoryConfig& config) noexcept
    {
        InitResolution resolution{};
        resolution.effectiveConfig = config;

        const std::size_t rawFrameBytes = resolution.effectiveConfig.thread_frame_allocator_bytes;
        resolution.threadFrameBytes = (rawFrameBytes == 0u)
            ? 0u
            : ::dng::core::AlignUp<std::size_t>(rawFrameBytes, alignof(std::max_align_t));

        resolution.threadFrameConfig.bReturnNullOnOOM = resolution.effectiveConfig.thread_frame_return_null;
        resolution.threadFrameConfig.bDebugPoisonOnReset = resolution.effectiveConfig.thread_frame_poison_on_reset;
        resolution.threadFrameConfig.DebugPoisonByte = resolution.effectiveConfig.thread_frame_poison_value;

        resolution.effectiveConfig.thread_frame_allocator_bytes = resolution.threadFrameBytes;
        resolution.effectiveConfig.thread_frame_return_null = resolution.threadFrameConfig.bReturnNullOnOOM;
        resolution.effectiveConfig.thread_frame_poison_on_reset = resolution.threadFrameConfig.bDebugPoisonOnReset;
        resolution.effectiveConfig.thread_frame_poison_value = resolution.threadFrameConfig.DebugPoisonByte;

        resolution.sampling = ResolveTrackingSampling(resolution.effectiveConfig);
        resolution.shards = ResolveTrackingShards(resolution.effectiveConfig);
        resolution.batch = ResolveSmallObjectBatch(resolution.effectiveConfig);

        std::uint32_t effectiveSampling = resolution.sampling.value;
        if (effectiveSampling == 0u || effectiveSampling > 1u)
        {
            effectiveSampling = 1u;
        }

        std::uint32_t effectiveShards = resolution.shards.value;
        if (!::dng::core::IsPowerOfTwo(effectiveShards))
        {
            effectiveShards = static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);
        }

        resolution.trackingCompiled = ::dng::core::CompiledTracking() || ::dng::core::CompiledStatsOnly();
        resolution.guardsCompiled = ::dng::core::CompiledGuards();
        resolution.tlsBinsCompiled = (DNG_SMALLOBJ_TLS_BINS != 0);
        resolution.trackingRequested = resolution.effectiveConfig.enable_tracking;
        resolution.guardsRequested = resolution.effectiveConfig.enable_guards;
        resolution.tlsBinsRequested = resolution.effectiveConfig.enable_smallobj_tls_bins;

        resolution.effectiveConfig.tracking_sampling_rate = effectiveSampling;
        resolution.effectiveConfig.tracking_shard_count = effectiveShards;
        resolution.effectiveConfig.small_object_batch = resolution.batch.value;
        resolution.effectiveConfig.enable_tracking =
            resolution.trackingCompiled && resolution.trackingRequested;
        resolution.effectiveConfig.enable_guards =
            resolution.guardsCompiled && resolution.guardsRequested;
        resolution.effectiveConfig.enable_smallobj_tls_bins =
            resolution.tlsBinsCompiled && resolution.tlsBinsRequested;

        return resolution;
    }

    [[nodiscard]] bool AreEquivalentMemoryConfigs(const MemoryConfig& lhs, const MemoryConfig& rhs) noexcept
    {
        return lhs.enable_tracking == rhs.enable_tracking &&
               lhs.enable_stats_only == rhs.enable_stats_only &&
               lhs.fatal_on_oom == rhs.fatal_on_oom &&
               lhs.enable_guards == rhs.enable_guards &&
               lhs.poison_on_free == rhs.poison_on_free &&
               lhs.capture_callsite == rhs.capture_callsite &&
               lhs.report_on_exit == rhs.report_on_exit &&
               lhs.global_thread_safe == rhs.global_thread_safe &&
               lhs.global_thread_policy == rhs.global_thread_policy &&
               lhs.enable_smallobj_tls_bins == rhs.enable_smallobj_tls_bins &&
               lhs.tracking_sampling_rate == rhs.tracking_sampling_rate &&
               lhs.tracking_shard_count == rhs.tracking_shard_count &&
               lhs.small_object_batch == rhs.small_object_batch &&
               lhs.thread_frame_allocator_bytes == rhs.thread_frame_allocator_bytes &&
               lhs.thread_frame_return_null == rhs.thread_frame_return_null &&
               lhs.thread_frame_poison_on_reset == rhs.thread_frame_poison_on_reset &&
               lhs.thread_frame_poison_value == rhs.thread_frame_poison_value &&
               lhs.collect_stacks == rhs.collect_stacks;
    }

    [[nodiscard]] AllocatorRef MakeAllocatorRef(DefaultAllocator* alloc) noexcept
    {
        return AllocatorRef(static_cast<::dng::core::IAllocator*>(alloc));
    }

    [[nodiscard]] AllocatorRef MakeAllocatorRef(TrackingAllocator* alloc) noexcept
    {
        return AllocatorRef(static_cast<::dng::core::IAllocator*>(alloc));
    }

    [[nodiscard]] AllocatorRef MakeAllocatorRef(SmallObjectAllocator* alloc) noexcept
    {
        return AllocatorRef(static_cast<::dng::core::IAllocator*>(alloc));
    }

    void LogInitWarnings(const InitResolution& resolution, bool warnEnabled) noexcept
    {
        if (!warnEnabled)
        {
            return;
        }

        constexpr std::uint32_t kMaxSmallBatch = static_cast<std::uint32_t>(DNG_SOA_TLS_MAG_CAPACITY);

        if (resolution.trackingRequested && !resolution.trackingCompiled)
        {
            DNG_LOG_WARNING("Memory",
                "Ignoring MemoryConfig::enable_tracking request (tracking/statistics compiled out).");
        }
        if (resolution.guardsRequested && !resolution.guardsCompiled)
        {
            DNG_LOG_WARNING("Memory",
                "Ignoring MemoryConfig::enable_guards request (DNG_MEM_GUARDS=0).");
        }
        if (resolution.sampling.value > 1u)
        {
            DNG_LOG_WARNING("Memory",
                "Tracking sampling rates >1 are not yet supported; falling back to 1 (requested {}).",
                static_cast<unsigned long long>(resolution.sampling.value));
        }
        if (resolution.sampling.envInvalid)
        {
            DNG_LOG_WARNING("Memory", "Ignoring DNG_MEM_TRACKING_SAMPLING_RATE environment override (must be >= 1).");
        }
        if (resolution.sampling.apiInvalid)
        {
            DNG_LOG_WARNING("Memory",
                "Ignoring MemoryConfig::tracking_sampling_rate override {} (must be >= 1).",
                static_cast<unsigned long long>(resolution.sampling.apiRaw));
        }

        if (resolution.shards.envInvalid)
        {
            if (resolution.shards.envRaw != 0u)
            {
                DNG_LOG_WARNING("Memory",
                    "Ignoring DNG_MEM_TRACKING_SHARDS environment override {} (must be power-of-two).",
                    static_cast<unsigned long long>(resolution.shards.envRaw));
            }
            else
            {
                DNG_LOG_WARNING("Memory",
                    "Ignoring DNG_MEM_TRACKING_SHARDS environment override (must be power-of-two).");
            }
        }
        if (resolution.shards.apiInvalid)
        {
            DNG_LOG_WARNING("Memory",
                "Ignoring MemoryConfig::tracking_shard_count override {} (must be power-of-two).",
                static_cast<unsigned long long>(resolution.shards.apiRaw));
        }
        if (resolution.shards.clamped && !resolution.shards.envInvalid && !resolution.shards.apiInvalid)
        {
            DNG_LOG_WARNING("Memory",
                "Tracking shard count fell back to compile-time default {} (invalid override).",
                static_cast<unsigned long long>(static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS)));
        }

        if (resolution.batch.envInvalid)
        {
            DNG_LOG_WARNING("Memory", "Ignoring DNG_SOALLOC_BATCH environment override (must be >= 1).");
        }
        if (resolution.batch.apiInvalid)
        {
            DNG_LOG_WARNING("Memory",
                "Ignoring MemoryConfig::small_object_batch override {} (must be >= 1).",
                static_cast<unsigned long long>(resolution.batch.apiRaw));
        }
        if (resolution.batch.clamped)
        {
            switch (resolution.batch.source)
            {
            case OverrideSource::Environment:
                DNG_LOG_WARNING("Memory",
                    "Clamped DNG_SOALLOC_BATCH override {} to {} (max capacity {}).",
                    static_cast<unsigned long long>(resolution.batch.envRaw),
                    static_cast<unsigned long long>(resolution.batch.value),
                    static_cast<unsigned long long>(kMaxSmallBatch));
                break;
            case OverrideSource::Api:
                DNG_LOG_WARNING("Memory",
                    "Clamped MemoryConfig::small_object_batch override {} to {} (max capacity {}).",
                    static_cast<unsigned long long>(resolution.batch.apiRaw),
                    static_cast<unsigned long long>(resolution.batch.value),
                    static_cast<unsigned long long>(kMaxSmallBatch));
                break;
            default:
                DNG_LOG_WARNING("Memory",
                    "SmallObject batch default exceeded capacity; clamped to {}.",
                    static_cast<unsigned long long>(resolution.batch.value));
                break;
            }
        }

#if !DNG_SMALLOBJ_TLS_BINS
        if (resolution.tlsBinsRequested)
        {
            DNG_LOG_WARNING("Memory",
                "Ignoring MemoryConfig::enable_smallobj_tls_bins request (DNG_SMALLOBJ_TLS_BINS=0).");
        }
#endif
    }

    void LogInitSummary(const MemoryGlobals& globals, const InitResolution& resolution) noexcept
    {
        DNG_LOG_INFO("Memory",
            "MemorySystem initialized (Tracking={}, ThreadSafe={})",
            globals.activeConfig.enable_tracking ? "true" : "false",
            globals.activeConfig.global_thread_safe ? "true" : "false");
        DNG_LOG_INFO("Memory",
            "Tracking sampling rate={} (source={})",
            static_cast<unsigned long long>(globals.activeConfig.tracking_sampling_rate),
            ToString(resolution.sampling.source));
        DNG_LOG_INFO("Memory",
            "Tracking shard count={} (source={})",
            static_cast<unsigned long long>(globals.activeConfig.tracking_shard_count),
            ToString(resolution.shards.source));
        DNG_LOG_INFO("Memory",
            "SmallObject TLS batch={} (source={})",
            static_cast<unsigned long long>(globals.activeConfig.small_object_batch),
            ToString(resolution.batch.source));
        DNG_LOG_INFO("Memory",
            "Thread frame allocator bytes={} returnNull={} poisonOnReset={} poisonValue={}",
            static_cast<unsigned long long>(globals.threadFrameBytes),
            globals.threadFrameConfig.bReturnNullOnOOM ? "true" : "false",
            globals.threadFrameConfig.bDebugPoisonOnReset ? "true" : "false",
            static_cast<unsigned>(globals.threadFrameConfig.DebugPoisonByte));
        DNG_LOG_INFO("Memory",
            "SMALLOBJ_TLS_BINS: CT={} RT={} EFFECTIVE={}",
            DNG_SMALLOBJ_TLS_BINS ? "1" : "0",
            resolution.tlsBinsRequested ? "1" : "0",
            globals.activeConfig.enable_smallobj_tls_bins ? "1" : "0");
        DNG_LOG_INFO("Memory",
            "MemorySystem: Tracking CT={} RT={} EFFECTIVE={}",
            resolution.trackingCompiled ? "1" : "0",
            resolution.trackingRequested ? "1" : "0",
            globals.activeConfig.enable_tracking ? "1" : "0");
        DNG_LOG_INFO("Memory",
            "MemorySystem: GuardAllocator CT={} RT={} EFFECTIVE={}",
            resolution.guardsCompiled ? "1" : "0",
            resolution.guardsRequested ? "1" : "0",
            globals.activeConfig.enable_guards ? "1" : "0");
    }

    void DestroyGlobals(MemoryGlobals& globals) noexcept
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

        globals.threadFrameConfig = FrameAllocatorConfig{};
        globals.threadFrameBytes = 0;
        globals.activeConfig = MemoryConfig{};
        globals.initialized = false;
    }

    void AttachThreadStateUnlocked(MemoryGlobals& globals) noexcept
    {
        if (gThreadLocalState.attached)
        {
            return;
        }

        const AllocatorRef smallObjectRef = MakeAllocatorRef(globals.smallObjectAllocator);

        FrameAllocator* frame = nullptr;
        void* frameBacking = nullptr;
        const std::size_t frameBytes = globals.threadFrameBytes;

        if (frameBytes != 0u)
        {
            DNG_CHECK(globals.defaultAllocator != nullptr && "Default allocator required for frame backing");
            const std::size_t alignment = ::dng::core::NormalizeAlignment(alignof(std::max_align_t));
            frameBacking = globals.defaultAllocator->Allocate(frameBytes, alignment);
            if (!frameBacking)
            {
                DNG_MEM_CHECK_OOM(frameBytes, alignment, "MemorySystem::AttachThreadState frame backing");
            }
            else
            {
                auto* storage = reinterpret_cast<FrameAllocator*>(gThreadLocalState.frameStorage);
                frame = new (storage) FrameAllocator(frameBacking, frameBytes, globals.threadFrameConfig);
            }
        }

        if ((frameBytes != 0u) && (frame == nullptr))
        {
            return;
        }

        gThreadLocalState.smallObject = smallObjectRef;
        gThreadLocalState.frameAllocator = frame;
        gThreadLocalState.frameBacking = frameBacking;
        gThreadLocalState.frameCapacity = frameBytes;
        gThreadLocalState.attached = true;

#if DNG_MEM_THREAD_SAFE
        ++globals.attachedThreads;
#endif
    }

    void DetachThreadStateUnlocked(MemoryGlobals& globals) noexcept
    {
        if (!gThreadLocalState.attached)
        {
            return;
        }

        if (gThreadLocalState.frameAllocator)
        {
            gThreadLocalState.frameAllocator->Reset();
            gThreadLocalState.frameAllocator->~FrameAllocator();
            gThreadLocalState.frameAllocator = nullptr;
        }

        if (gThreadLocalState.frameBacking)
        {
            if (globals.defaultAllocator)
            {
                const std::size_t alignment = ::dng::core::NormalizeAlignment(alignof(std::max_align_t));
                globals.defaultAllocator->Deallocate(gThreadLocalState.frameBacking,
                    gThreadLocalState.frameCapacity,
                    alignment);
            }
            gThreadLocalState.frameBacking = nullptr;
        }

        gThreadLocalState.frameCapacity = 0;

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

void MemorySystem::Init(const MemoryConfig& config) noexcept
{
    auto& globals = detail::Globals();
    detail::ThreadLock lock(globals.mutex);
    const detail::InitResolution resolution = detail::ResolveInitConfig(config);

    if (globals.initialized)
    {
        if (!detail::AreEquivalentMemoryConfigs(globals.activeConfig, resolution.effectiveConfig) &&
            ::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory"))
        {
            DNG_LOG_WARNING("Memory",
                "MemorySystem::Init() ignored because the requested config does not match the active config.");
        }
        return;
    }

    auto& globalConfig = ::dng::core::MemoryConfig::GetGlobal();
    globalConfig = resolution.effectiveConfig;
    globals.threadFrameBytes = resolution.threadFrameBytes;
    globals.threadFrameConfig = resolution.threadFrameConfig;
    globals.activeConfig = resolution.effectiveConfig;

    const bool warnEnabled = ::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory");

    ::dng::core::SetFatalOnOOMPolicy(globals.activeConfig.fatal_on_oom);
    detail::LogInitWarnings(resolution, warnEnabled);

    globals.defaultAllocator = new (globals.defaultAllocatorStorage) detail::DefaultAllocator();
    auto* effectiveParent = static_cast<::dng::core::IAllocator*>(globals.defaultAllocator);

    const std::uint32_t trackingSamplingRate = globals.activeConfig.tracking_sampling_rate != 0u
        ? globals.activeConfig.tracking_sampling_rate
        : static_cast<std::uint32_t>(DNG_MEM_TRACKING_SAMPLING_RATE);
    const std::uint32_t trackingShardCount =
        (globals.activeConfig.tracking_shard_count != 0u &&
            ::dng::core::IsPowerOfTwo(globals.activeConfig.tracking_shard_count))
        ? globals.activeConfig.tracking_shard_count
        : static_cast<std::uint32_t>(DNG_MEM_TRACKING_SHARDS);

    if (globals.activeConfig.enable_tracking)
    {
        globals.trackingAllocator = new (globals.trackingAllocatorStorage)
            detail::TrackingAllocator(globals.defaultAllocator, trackingSamplingRate, trackingShardCount);
        effectiveParent = static_cast<::dng::core::IAllocator*>(globals.trackingAllocator);
    }

#if DNG_MEM_GUARDS
    if (globals.activeConfig.enable_guards)
    {
        globals.guardAllocator = new (globals.guardAllocatorStorage) detail::GuardAllocator(effectiveParent);
        effectiveParent = static_cast<::dng::core::IAllocator*>(globals.guardAllocator);
    }
#endif

    ::dng::core::SmallObjectConfig smallCfg{};
    smallCfg.ReturnNullOnOOM = !globals.activeConfig.fatal_on_oom;
    smallCfg.TLSBatchSize = static_cast<std::size_t>(globals.activeConfig.small_object_batch);
    smallCfg.EnableTLSBins = globals.activeConfig.enable_smallobj_tls_bins;
    globals.smallObjectAllocator =
        new (globals.smallObjectStorage) detail::SmallObjectAllocator(effectiveParent, smallCfg);

    globals.rendererArena = new (globals.rendererArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kRendererArenaBytes);
    if (!globals.rendererArena->IsValid())
    {
        DNG_MEM_CHECK_OOM(detail::kRendererArenaBytes, alignof(detail::ArenaAllocator), "MemorySystem::Init rendererArena");
    }

    globals.audioArena = new (globals.audioArenaStorage) detail::ArenaAllocator(effectiveParent, detail::kAudioArenaBytes);
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

    if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Info, "Memory"))
    {
        detail::LogInitSummary(globals, resolution);
    }

    detail::AttachThreadStateUnlocked(globals);
    if (!detail::gThreadLocalState.attached && warnEnabled)
    {
        DNG_LOG_WARNING("Memory",
            "MemorySystem::Init() could not attach thread-local allocators (frame provisioning failed).");
    }
}

void MemorySystem::Shutdown() noexcept
{
    auto& globals = detail::Globals();
    detail::ThreadLock lock(globals.mutex);

    if (!globals.initialized)
    {
        return;
    }

    detail::DetachThreadStateUnlocked(globals);

#if DNG_MEM_THREAD_SAFE
    if (globals.attachedThreads != 0 &&
        ::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory"))
    {
        DNG_LOG_WARNING("Memory",
            "MemorySystem::Shutdown() detected {} threads still attached.",
            static_cast<unsigned long long>(globals.attachedThreads));
    }
#endif

    detail::DestroyGlobals(globals);
    ::dng::core::MemoryConfig::GetGlobal() = MemoryConfig{};
    ::dng::core::SetFatalOnOOMPolicy(::dng::core::MemoryConfig::GetGlobal().fatal_on_oom);
}

void MemorySystem::OnThreadAttach() noexcept
{
    auto& globals = detail::Globals();
    detail::ThreadLock lock(globals.mutex);

    if (!globals.initialized)
    {
        if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, "Memory"))
        {
            DNG_LOG_WARNING("Memory", "OnThreadAttach() ignored: MemorySystem not initialized.");
        }
        return;
    }

    detail::AttachThreadStateUnlocked(globals);
}

void MemorySystem::OnThreadDetach() noexcept
{
    auto& globals = detail::Globals();
    detail::ThreadLock lock(globals.mutex);

    if (!globals.initialized)
    {
        return;
    }

    detail::DetachThreadStateUnlocked(globals);
}

bool MemorySystem::TryGetActiveConfig(MemoryConfig& outConfig) noexcept
{
    auto& globals = detail::Globals();
    detail::ThreadLock lock(globals.mutex);

    if (!globals.initialized)
    {
        return false;
    }

    outConfig = globals.activeConfig;
    return true;
}

bool MemorySystem::IsConfigCompatible(const MemoryConfig& config) noexcept
{
    auto& globals = detail::Globals();
    detail::ThreadLock lock(globals.mutex);

    if (!globals.initialized)
    {
        return false;
    }

    const detail::InitResolution resolution = detail::ResolveInitConfig(config);
    return detail::AreEquivalentMemoryConfigs(globals.activeConfig, resolution.effectiveConfig);
}

bool MemorySystem::IsInitialized() noexcept
{
    return detail::Globals().initialized;
}

AllocatorRef MemorySystem::GetDefaultAllocator() noexcept
{
    return detail::MakeAllocatorRef(detail::Globals().defaultAllocator);
}

AllocatorRef MemorySystem::GetTrackingAllocator() noexcept
{
    return detail::MakeAllocatorRef(detail::Globals().trackingAllocator);
}

AllocatorRef MemorySystem::GetSmallObjectAllocator() noexcept
{
    return detail::MakeAllocatorRef(detail::Globals().smallObjectAllocator);
}

::dng::core::FrameAllocator& MemorySystem::GetThreadFrameAllocator() noexcept
{
    DNG_CHECK(IsInitialized() && "MemorySystem::GetThreadFrameAllocator requires Init()");
    const auto& globals = detail::Globals();
    DNG_CHECK(globals.threadFrameBytes != 0 && "Thread frame allocator disabled via config");

    auto& state = detail::gThreadLocalState;
    if (!state.attached)
    {
        OnThreadAttach();
    }

    DNG_ASSERT(state.attached && state.frameAllocator != nullptr && "Thread frame allocator unavailable");
    return *state.frameAllocator;
}

} // namespace memory
} // namespace dng
