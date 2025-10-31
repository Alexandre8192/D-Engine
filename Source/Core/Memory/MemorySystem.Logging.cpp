// ============================================================================
// D-Engine - Core/Memory/MemorySystem.Logging.cpp
// ----------------------------------------------------------------------------
// Purpose : Implement the slow-path formatting helpers declared in
//           MemorySystem.hpp so headers stay lean and hot paths avoid logging
//           dependencies.
// Contract: No allocations or blocking work outside of guarded log calls. All
//           functions are `noexcept` and rely on the `MemorySystem` internals
//           to provide valid inputs.
// Notes   : Heavy diagnostics stay in this TU to keep the header hot path
//           minimal; no new vtables or exceptions are introduced.
// ============================================================================

#include "Core/Memory/MemorySystem.hpp"

#include <cstdint>

namespace dng
{
namespace memory
{
namespace detail
{

void LogInitWarnings(const OverrideResult& sampling,
    const OverrideResult& shards,
    const OverrideResult& batch,
    bool tlsBinsRequested,
    bool tlsBinsCompiled,
    std::uint32_t maxSmallBatch) noexcept
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
            DNG_LOG_WARNING("Memory",
                "Ignoring DNG_MEM_TRACKING_SHARDS environment override (must be power-of-two).");
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
        case OverrideSource::Environment:
            DNG_LOG_WARNING("Memory",
                "Clamped DNG_SOALLOC_BATCH override {} to {} (max capacity {}).",
                static_cast<unsigned long long>(batch.envRaw),
                static_cast<unsigned long long>(batch.value),
                static_cast<unsigned long long>(maxSmallBatch));
            break;
        case OverrideSource::Api:
            DNG_LOG_WARNING("Memory",
                "Clamped MemoryConfig::small_object_batch override {} to {} (max capacity {}).",
                static_cast<unsigned long long>(batch.apiRaw),
                static_cast<unsigned long long>(batch.value),
                static_cast<unsigned long long>(maxSmallBatch));
            break;
        default:
            DNG_LOG_WARNING("Memory",
                "SmallObject batch default exceeded capacity; clamped to {}.",
                static_cast<unsigned long long>(batch.value));
            break;
        }
    }

    if (tlsBinsRequested && !tlsBinsCompiled)
    {
        DNG_LOG_WARNING("Memory",
            "Ignoring MemoryConfig::enable_smallobj_tls_bins request (DNG_SMALLOBJ_TLS_BINS=0).");
    }
}

void LogInitSummary(const MemoryConfig& config,
    const OverrideResult& sampling,
    const OverrideResult& shards,
    const OverrideResult& batch,
    std::uint32_t trackingSamplingRate,
    std::uint32_t trackingShardCount,
    bool tlsBinsRequested,
    bool tlsBinsEffective,
    bool guardsEnabled) noexcept
{
    DNG_LOG_INFO("Memory",
        "MemorySystem initialized (Tracking={}, ThreadSafe={})",
        config.enable_tracking ? "true" : "false",
        config.global_thread_safe ? "true" : "false");
    DNG_LOG_INFO("Memory",
        "Tracking sampling rate={} (source={})",
        static_cast<unsigned long long>(trackingSamplingRate),
    ToString(sampling.source));
    DNG_LOG_INFO("Memory",
        "Tracking shard count={} (source={})",
        static_cast<unsigned long long>(trackingShardCount),
    ToString(shards.source));
    DNG_LOG_INFO("Memory",
        "SmallObject TLS batch={} (source={})",
        static_cast<unsigned long long>(config.small_object_batch),
    ToString(batch.source));
    DNG_LOG_INFO("Memory",
        "SMALLOBJ_TLS_BINS: CT={} RT={} EFFECTIVE={}",
        DNG_SMALLOBJ_TLS_BINS ? "1" : "0",
        tlsBinsRequested ? "1" : "0",
        tlsBinsEffective ? "1" : "0");

    DNG_LOG_INFO("Memory",
        "MemorySystem: GuardAllocator {}",
        guardsEnabled ? "ENABLED" : "DISABLED");
}

void LogArenaDestruction(const char* label, const ArenaAllocator* arena) noexcept
{
    if (!arena)
    {
        return;
    }

    DNG_LOG_INFO("Memory",
        "DestroyGlobals: destroying {} arena (ptr={}, capacity={}, valid={})",
        label,
        static_cast<const void*>(arena),
        static_cast<unsigned long long>(arena->GetCapacity()),
        arena->IsValid() ? "true" : "false");
}

void LogAllocatorDestruction(const char* label) noexcept
{
    DNG_LOG_INFO("Memory", "DestroyGlobals: destroying {}", label);
}

void LogLeakReport() noexcept
{
    DNG_LOG_INFO("Memory", "DestroyGlobals: reporting leaks");
}

} // namespace detail
} // namespace memory
} // namespace dng
