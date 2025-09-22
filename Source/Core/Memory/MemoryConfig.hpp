// ============================================================================
// MemoryConfig.hpp - D-Engine
// Compile-time feature gates + lightweight runtime toggles (no heavy deps)
// ============================================================================
//
// Philosophy:
// - Compile-time macros decide what code is compiled in/out (true zero-cost).
// - Runtime toggles only have effect if their feature is compiled in.
// - When a feature is compiled out, toggles become explicit no-ops with logs.
// - No heavy dependencies; safe to include from anywhere.
//
// ============================================================================

#pragma once

// -----------------------------------------------------------------------------
// Minimal logging fallbacks (avoid dependency on Logger.hpp here)
// -----------------------------------------------------------------------------
#ifndef DNG_LOG_WARNING
#   define DNG_LOG_WARNING(category, fmt, ...) ((void)0)
#endif
#ifndef DNG_LOG_ERROR
#   define DNG_LOG_ERROR(category, fmt, ...) ((void)0)
#endif

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

// ============================================================================
// Compile-time "capabilities" view (constexpr booleans)
// These are the **ground truth** used throughout the engine.
// ============================================================================
namespace dng::core
{
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
// ---------------------------------------------------------------------------
//
// Notes:
// - If Full Tracking is compiled in and enabled, it can supersede/augment Stats Only.
// - Prefer per-allocator thread policies for hot paths. The global knob is coarse.
// - When CT=0, we log explicit no-ops so users understand why toggles do nothing.
//
// ============================================================================






