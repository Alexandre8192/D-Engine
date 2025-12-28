// ============================================================================
// D-Engine - Core/Memory/MemoryConfig.hpp
// ----------------------------------------------------------------------------
// Purpose : Centralize compile-time memory feature gates and lightweight runtime
//           knobs used by the memory subsystem across the engine. Uses the engine
//           logging front-end; no local fallbacks.
// Contract: Header-only, self-contained, and safe to include from any TU. No
//           hidden dependencies or global state. Compile-time macros define the
//           compiled feature set; runtime toggles only take effect when their
//           feature is compiled in. When a feature is compiled out, setters are
//           explicit no-ops that log a warning. Invariants validated via static_assert.
//           This header requires the logging front-end via Logger.hpp; no macro
//           redefinition occurs here.
// Notes   : Optimized for determinism and zero hidden costs. Runtime precedence
//           for tunables is API -> environment -> macros; see MemorySystem.hpp for
//           resolution and logging. Defaults reflect Release | x64 bench sweeps
//           (2025-10-29) with stable ns/op and unchanged bytes/allocs. We avoid
//           shadowing `DNG_LOG_*` to prevent silent diagnostic loss due to include order.
// ============================================================================

#pragma once

#include "Core/Logger.hpp"

#include <cstddef>
#include <cstdint>

#include "Core/Memory/MemMacros.hpp"

#ifndef DNG_MEM_LOG_CATEGORY
#define DNG_MEM_LOG_CATEGORY "Memory"
#endif

// Optional: memory log verbosity (0=silent, 1=info, 2=debug)
#ifndef DNG_MEM_LOG_VERBOSITY
#if defined(NDEBUG)
#define DNG_MEM_LOG_VERBOSITY 0
#else
#define DNG_MEM_LOG_VERBOSITY 1
#endif
#endif

// -----------------------------------------------------------------------------
// Defaults for compile-time gates (Dev-defaults vs Release-defaults)
// You can override these in your build system or before including this header.
// -----------------------------------------------------------------------------

// Tracking & leak detection
#ifndef DNG_MEM_TRACKING
#   if !defined(NDEBUG)
#       define DNG_MEM_TRACKING 1
#   else
#       define DNG_MEM_TRACKING 0
#   endif
#endif

// Lightweight counters without full tracking
#ifndef DNG_MEM_STATS_ONLY
#   define DNG_MEM_STATS_ONLY 0
#endif

// Out-of-memory strategy: fatal (assert/log) vs returning nullptr/throwing
#ifndef DNG_MEM_FATAL_ON_OOM
#   define DNG_MEM_FATAL_ON_OOM 0
#endif

// Guard regions/patterns (can add redzones in debug)
#ifndef DNG_MEM_GUARDS
#   if !defined(NDEBUG)
#       define DNG_MEM_GUARDS 1
#   else
#       define DNG_MEM_GUARDS 0
#   endif
#endif

// Poison memory on free (debug visualization aid)
#ifndef DNG_MEM_POISON_ON_FREE
#   define DNG_MEM_POISON_ON_FREE 0
#endif

// Capture allocation callsite (__FILE__/__LINE__) in dev
#ifndef DNG_MEM_CAPTURE_CALLSITE
#   if !defined(NDEBUG)
#       define DNG_MEM_CAPTURE_CALLSITE 1
#   else
#       define DNG_MEM_CAPTURE_CALLSITE 0
#   endif
#endif

// Emit a report on exit (quiet release)
#ifndef DNG_MEM_REPORT_ON_EXIT
#   if !defined(NDEBUG)
#       define DNG_MEM_REPORT_ON_EXIT 1
#   else
#       define DNG_MEM_REPORT_ON_EXIT 0
#   endif
#endif

// Global thread-safe variants (prefer per-type policy in production)
#ifndef DNG_MEM_THREAD_SAFE
#   define DNG_MEM_THREAD_SAFE 0
#endif

// Thread policy selector: 0 = none, 1 = mutex
#ifndef DNG_MEM_THREAD_POLICY
#   define DNG_MEM_THREAD_POLICY 1
#endif

// Sanity checks on policy range
#if (DNG_MEM_THREAD_POLICY != 0) && (DNG_MEM_THREAD_POLICY != 1)
#   error "DNG_MEM_THREAD_POLICY must be 0 (none) or 1 (mutex)."
#endif

// -----------------------------------------------------------------------------
// Bench-derived production defaults (Release | x64) - see defaults.json
// Sweep reference: artifacts/bench/sweeps/ (2025-10-29) -> docs/Memory.md
// -----------------------------------------------------------------------------
#ifndef DNG_MEM_TRACKING_SAMPLING_RATE
// Purpose : Release sampling default chosen from sweep combo s1-h8-b64 (2025-10-29).
// Contract: Only applies when the macro is left undefined; toolchains may override
//           for debugging/instrumentation prior to including MemoryConfig.hpp.
// Notes   : TrackingAllocator median=203.639 ns/op (delta=-17.404%, -42.909 ns vs
//           s1-h1-b32 baseline) with bytes/allocs stable at 64/1. See defaults.md.
#   define DNG_MEM_TRACKING_SAMPLING_RATE 1
#endif

#ifndef DNG_MEM_TRACKING_SHARDS
// Purpose : Release shard count default aligned with sweep best pick s1-h8-b64.
// Contract: May be overridden before including this header; Release builds rely on
//           this value to keep TrackingAllocator contention bounded without hidden costs.
// Notes   : Secondary metrics stayed within -1.561% (tracking_vector PushPop) and
//           -25.282% (Arena 64B) while bytes/allocs remained unchanged. See defaults.md.
#   define DNG_MEM_TRACKING_SHARDS 8
#endif

#ifndef DNG_SOALLOC_BATCH
// Purpose : Release batch size default for SmallObjectAllocator verified on 2025-10-29 sweeps.
// Contract: Overridable by integrators; applies whenever SmallObjectAllocator is compiled in
//           and no alternative batch is forced prior to including this header.
// Notes   : SmallObject 64B median=26.466 ns/op (delta=+0.096 ns, +0.364%) with identical
//           bytes/allocs (0/0) against the s1-h1-b32 baseline. Cross-check docs/Memory.md.
#   define DNG_SOALLOC_BATCH 64
#endif

// --- Sanity checks for core switches ---
#if (DNG_MEM_TRACKING != 0) && (DNG_MEM_TRACKING != 1)
#   error "DNG_MEM_TRACKING must be 0 or 1"
#endif

#if (DNG_MEM_LOG_VERBOSITY < 0) || (DNG_MEM_LOG_VERBOSITY > 2)
#   error "DNG_MEM_LOG_VERBOSITY must be 0 (silent), 1 (info), or 2 (debug)"
#endif

#if (DNG_MEM_FATAL_ON_OOM != 0) && (DNG_MEM_FATAL_ON_OOM != 1)
#   error "DNG_MEM_FATAL_ON_OOM must be 0 (non-fatal) or 1 (fatal)"
#endif

#if (DNG_MEM_CAPTURE_CALLSITE != 0) && (DNG_MEM_CAPTURE_CALLSITE != 1)
#   error "DNG_MEM_CAPTURE_CALLSITE must be 0 or 1"
#endif

static_assert((DNG_MEM_TRACKING_SAMPLING_RATE) >= 1, "Tracking sampling rate must be >= 1");
static_assert((DNG_MEM_TRACKING_SHARDS) >= 1, "Tracking shard count must be >= 1");
static_assert(((DNG_MEM_TRACKING_SHARDS) & ((DNG_MEM_TRACKING_SHARDS) - 1)) == 0,
    "Tracking shard count must be a power of two");
static_assert((DNG_SOALLOC_BATCH) >= 1, "SmallObject batch must be >= 1");

// -----------------------------------------------------------------------------
// New: paranoia/meta header toggle (Dev/Debug-friendly)
// 0 = store minimal header (rawPtr+magic)  [default]
// 1 = also store size+align in header      (runtime checks on free/realloc)
// -----------------------------------------------------------------------------
#ifndef DNG_MEM_PARANOID_META
#   define DNG_MEM_PARANOID_META 0
#endif
#if (DNG_MEM_PARANOID_META != 0) && (DNG_MEM_PARANOID_META != 1)
#   error "DNG_MEM_PARANOID_META must be 0 or 1"
#endif

// -----------------------------------------------------------------------------
// New: global cap for "reasonable" alignments across all allocators
// Use a power-of-two to preserve alignment properties. Default: 1 MiB.
// -----------------------------------------------------------------------------
#ifndef DNG_MAX_REASONABLE_ALIGNMENT
#   define DNG_MAX_REASONABLE_ALIGNMENT (1u << 20)
#endif

// -----------------------------------------------------------------------------
// SmallObjectAllocator tunables: TLS magazine sizing + batch transfers.
// -----------------------------------------------------------------------------
#ifndef DNG_SOA_TLS_MAG_CAPACITY
#   define DNG_SOA_TLS_MAG_CAPACITY 64u
#endif

#ifndef DNG_SOA_TLS_BATCH_COUNT
#   define DNG_SOA_TLS_BATCH_COUNT 8u
#endif

#ifndef DNG_SOA_SHARD_COUNT
#   define DNG_SOA_SHARD_COUNT 8u
#endif

// ============================================================================
// Compile-time "capabilities" view (constexpr booleans)
// These are the **ground truth** used throughout the engine.
// ============================================================================
namespace dng::core
{
    void SetFatalOnOOMPolicy(bool fatal) noexcept;
#if !defined(DNG_CORE_MEMORYCONFIG_CONSTANTS_DEFINED)
#define DNG_CORE_MEMORYCONFIG_CONSTANTS_DEFINED
    // clang-format off
    constexpr bool CompiledTracking() noexcept { return DNG_MEM_TRACKING != 0; }
    constexpr bool CompiledStatsOnly() noexcept { return DNG_MEM_STATS_ONLY != 0; }
    constexpr bool CompiledFatalOnOOM() noexcept { return DNG_MEM_FATAL_ON_OOM != 0; }
    constexpr bool CompiledGuards() noexcept { return DNG_MEM_GUARDS != 0; }
    constexpr bool CompiledPoisonOnFree() noexcept { return DNG_MEM_POISON_ON_FREE != 0; }
    constexpr bool CompiledCaptureCallsite() noexcept { return DNG_MEM_CAPTURE_CALLSITE != 0; }
    constexpr bool CompiledReportOnExit() noexcept { return DNG_MEM_REPORT_ON_EXIT != 0; }
    constexpr bool CompiledThreadSafe() noexcept { return DNG_MEM_THREAD_SAFE != 0; }
    constexpr int  CompiledThreadPolicy() noexcept { return DNG_MEM_THREAD_POLICY; }
    // clang-format on

    // Invariants:
    // - If full tracking is enabled, stats-only is redundant at runtime (but allowed).
    // - Guards/poison/callsite/report are orthogonal and may be compiled in/out.

    // =========================================================================
    // Runtime toggles container
    //   NOTE: Toggles only take effect if the corresponding *compiled* flag
    //   is true. Otherwise, setters are no-ops and will log a warning.
    // =========================================================================
    struct MemoryConfig
    {
        // --- Runtime toggles (effective only if compiled in) -----------------
        bool enable_tracking = CompiledTracking();           // default: ON when compiled in
        bool enable_stats_only = CompiledStatsOnly();        // default: ON when compiled in
        bool fatal_on_oom = CompiledFatalOnOOM();            // default: ON when compiled in
        bool enable_guards = CompiledGuards();               // default: ON when compiled in
        bool poison_on_free = CompiledPoisonOnFree();        // default: ON when compiled in
        bool capture_callsite = CompiledCaptureCallsite();   // default: ON when compiled in
        bool report_on_exit = CompiledReportOnExit();        // default: ON when compiled in

        // Thread safety: prefer per-allocator policy; this is a coarse global knob.
        bool global_thread_safe = CompiledThreadSafe();
        int  global_thread_policy = CompiledThreadPolicy(); // 0 = none, 1 = mutex

        // Purpose : Runtime gate for TLS small-object bins. Default off, requires compile-time support.
        // Contract: Effective only when DNG_SMALLOBJ_TLS_BINS==1; otherwise setters warn and value remains false.
        // Notes   : MemorySystem applies the compile/runtime truth table before instantiating SmallObjectAllocator.
        bool enable_smallobj_tls_bins = false;

        // Purpose : Optional runtime override for tracking sampling (0 preserves env/macro defaults).
        // Contract: Value sanitized to >=1 during MemorySystem::Init; API overrides win over env, which win over macros.
        // Notes   : Release builds typically leave this at 0 and rely on compile-time defaults to avoid redundant config.
        //           Values greater than 1 currently fall back to 1 with a warning until sampling support lands.
        std::uint32_t tracking_sampling_rate = 0;

        // Purpose : Optional runtime override for tracking allocator shard count (0 preserves env/macro defaults).
        // Contract: Sanitized to at least 1 and adjusted to the nearest power-of-two; precedence matches sampling.
        // Notes   : Non-power-of-two values fall back to the compile-time default with a warning.
        std::uint32_t tracking_shard_count = 0;

        // Purpose : Optional runtime override for SmallObjectAllocator magazine refill batch (0 keeps env/macro default).
        // Contract: Clamped to [1, DNG_SOA_TLS_MAG_CAPACITY]; precedence mirrors other knobs.
        // Notes   : Helps tune hot-path refill behaviour without rebuilding.
        std::uint32_t small_object_batch = 0;

        // Purpose : Configure the optional per-thread frame allocator used by MemorySystem::GetThreadFrameAllocator().
        // Contract: `thread_frame_allocator_bytes == 0` disables provisioning; MemorySystem normalizes alignment during Init().
        // Notes   : Poison toggles apply only when the allocator is active; DebugPoisonByte mirrors FrameAllocatorConfig defaults.
        std::size_t thread_frame_allocator_bytes = 0;
        bool thread_frame_return_null = true;
        bool thread_frame_poison_on_reset = false;
        std::uint8_t thread_frame_poison_value = 0xDD;

        // Purpose : Allow callers to suppress expensive stack collection even when full tracking is compiled in.
        // Contract: When false, TrackingAllocator skips map bookkeeping and only maintains counters.
        // Notes   : Defaults to true to preserve legacy behaviour; MemorySystem honours API override first.
        bool collect_stacks = true;

        // ---------------------------------------------------------------------
        // Singleton access
        // ---------------------------------------------------------------------
        static MemoryConfig& GetGlobal() noexcept
        {
            static MemoryConfig s_cfg{};
            return s_cfg;
        }

        // ---------------------------------------------------------------------
        // Setters with explicit "compiled-out" no-op behavior + logs
        // (Keep these inline to avoid ODR/ABI frictions.)
        // ---------------------------------------------------------------------
        void SetEnableTracking(bool v) noexcept
        {
            if constexpr (CompiledTracking())
            {
                enable_tracking = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Tracking was compiled out (DNG_MEM_TRACKING=0).");
            }
        }

        void SetEnableStatsOnly(bool v) noexcept
        {
            if constexpr (CompiledStatsOnly())
            {
                enable_stats_only = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Stats-only counters compiled out (DNG_MEM_STATS_ONLY=0).");
            }
        }

        void SetFatalOnOOM(bool v) noexcept
        {
            if constexpr (CompiledFatalOnOOM())
            {
                fatal_on_oom = v;
                SetFatalOnOOMPolicy(v);
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Fatal-on-OOM behavior compiled out (DNG_MEM_FATAL_ON_OOM=0).");
            }
        }

        void SetEnableGuards(bool v) noexcept
        {
            if constexpr (CompiledGuards())
            {
                enable_guards = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Guard regions compiled out (DNG_MEM_GUARDS=0).");
            }
        }

        void SetPoisonOnFree(bool v) noexcept
        {
            if constexpr (CompiledPoisonOnFree())
            {
                poison_on_free = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Poison-on-free compiled out (DNG_MEM_POISON_ON_FREE=0).");
            }
        }

        void SetCaptureCallsite(bool v) noexcept
        {
            if constexpr (CompiledCaptureCallsite())
            {
                capture_callsite = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Callsite capture compiled out (DNG_MEM_CAPTURE_CALLSITE=0).");
            }
        }

        void SetReportOnExit(bool v) noexcept
        {
            if constexpr (CompiledReportOnExit())
            {
                report_on_exit = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Report-on-exit compiled out (DNG_MEM_REPORT_ON_EXIT=0).");
            }
        }

        void SetGlobalThreadSafe(bool v) noexcept
        {
            if constexpr (CompiledThreadSafe())
            {
                global_thread_safe = v;
            }
            else
            {
                (void)v;
                DNG_LOG_WARNING("Memory", "[no-op] Global thread-safety compiled out (DNG_MEM_THREAD_SAFE=0).");
            }
        }

        void SetGlobalThreadPolicy(int policy) noexcept
        {
            if constexpr (CompiledThreadSafe())
            {
                if (policy == 0 || policy == 1)
                {
                    global_thread_policy = policy;
                }
                else
                {
                    DNG_LOG_ERROR("Memory", "Invalid thread policy (%d). Allowed: 0 (none), 1 (mutex).", policy);
                }
            }
            else
            {
                (void)policy;
                DNG_LOG_WARNING("Memory", "[no-op] Thread-policy ignored: thread safety compiled out.");
            }
        }

        void SetEnableSmallObjectTLSBins(bool v) noexcept
        {
#if DNG_SMALLOBJ_TLS_BINS
            enable_smallobj_tls_bins = v;
#else
            (void)v;
            DNG_LOG_WARNING("Memory", "[no-op] SmallObject TLS bins compiled out (DNG_SMALLOBJ_TLS_BINS=0).");
#endif
        }

        void SetThreadFrameAllocatorBytes(std::size_t bytes) noexcept
        {
            // Purpose : Record requested backing-store capacity for per-thread frame allocators.
            // Contract: Value is normalized during MemorySystem::Init; zero disables provisioning.
            // Notes   : Stored raw to keep this header dependency-light; alignment handled later.
            thread_frame_allocator_bytes = bytes;
        }

        void SetThreadFrameReturnNull(bool v) noexcept
        {
            // Purpose : Toggle soft-OOM behaviour for thread frame allocators.
            // Contract: When true, FrameAllocator::Allocate may yield nullptr; when false, OOM escalates via DNG_MEM_CHECK_OOM.
            // Notes   : Mirrors FrameAllocatorConfig::bReturnNullOnOOM; applied during MemorySystem::Init.
            thread_frame_return_null = v;
        }

        void SetThreadFramePoisonOnReset(bool v) noexcept
        {
            // Purpose : Enable debug poison fills when frame allocators Reset()/Rewind().
            // Contract: Extra cost applies only when the allocator is provisioned; respected by FrameScope lifetimes.
            // Notes   : Defaults off to preserve release performance; ideal for deterministic QA captures.
            thread_frame_poison_on_reset = v;
        }

        void SetThreadFramePoisonValue(std::uint8_t value) noexcept
        {
            // Purpose : Choose the debug fill byte used when poison-on-reset is active.
            // Contract: Value stored verbatim; caller ensures semantic meaning (0xDD, 0xFE, etc.).
            // Notes   : Applied to FrameAllocatorConfig::DebugPoisonByte during MemorySystem initialization.
            thread_frame_poison_value = value;
        }
    };

#endif // DNG_CORE_MEMORYCONFIG_CONSTANTS_DEFINED
} // namespace dng::core

// ============================================================================
//                       TRUTH TABLE (Grounded Reference)
// ============================================================================
//
// Legend:
//   CT  = Compile-time macro (DNG_*)
//   RT  = Runtime toggle (MemoryConfig)
//   Eff = Effective behavior in the engine (what actually happens)
//
// 1) Feature compiled OUT  (CT = 0)
//    - Any related RT setter: no-op + warning log
//    - Eff = OFF
//
// 2) Feature compiled IN   (CT = 1)
//    - RT = true  -> Eff = ON
//    - RT = false -> Eff = OFF
//
// ---------------------------------------------------------------------------
// Feature                 | CT Macro                  | RT Toggle                | Eff
// ------------------------|---------------------------|--------------------------|------------------------------
// Full Tracking           | DNG_MEM_TRACKING          | enable_tracking          | ON iff CT=1 AND RT=true
// Stats Only (light)      | DNG_MEM_STATS_ONLY        | enable_stats_only        | ON iff CT=1 AND RT=true
// Fatal on OOM            | DNG_MEM_FATAL_ON_OOM      | fatal_on_oom             | ON iff CT=1 AND RT=true
// Guard Regions           | DNG_MEM_GUARDS            | enable_guards            | ON iff CT=1 AND RT=true
// Poison on Free          | DNG_MEM_POISON_ON_FREE    | poison_on_free           | ON iff CT=1 AND RT=true
// Capture Callsite        | DNG_MEM_CAPTURE_CALLSITE  | capture_callsite         | ON iff CT=1 AND RT=true
// Report on Exit          | DNG_MEM_REPORT_ON_EXIT    | report_on_exit           | ON iff CT=1 AND RT=true
// Global Thread-Safety    | DNG_MEM_THREAD_SAFE       | global_thread_safe       | ON iff CT=1 AND RT=true
// Thread Policy           | DNG_MEM_THREAD_POLICY(0/1)| global_thread_policy(0/1)| Policy applies iff Thread-Safety Eff=ON
// SmallObject TLS Bins    | DNG_SMALLOBJ_TLS_BINS     | enable_smallobj_tls_bins | ON iff CT=1 AND RT=true
// ---------------------------------------------------------------------------
//
// Notes:
// - If Full Tracking is compiled in and enabled, it can supersede/augment Stats Only.
// - Prefer per-allocator thread policies for hot paths. The global knob is coarse.
// - When CT=0, we log explicit no-ops so users understand why toggles do nothing.
//
// ============================================================================
