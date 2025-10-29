#pragma once
// ============================================================================
// D-Engine - Core/Memory/OOM.hpp
// ----------------------------------------------------------------------------
// Purpose : Declare the engine-wide out-of-memory policy helpers invoked by
//           allocators and wrappers after allocation failure.
// Contract: All entry points are header-only, constexpr-safe, and noexcept;
//           they neither allocate nor throw and terminate deterministically
//           according to configuration.
// Notes   : Functions are inline to keep header self-contained; logging macros
//           are stubbed if the logger subsystem is not yet initialized.
// ============================================================================
#include "Core/CoreMinimal.hpp"  // Logger + Types + Platform
#include "Core/Memory/MemoryConfig.hpp" // compile-time defaults + runtime cfg
#include <cstdlib>          // std::abort

// Minimal logging fallbacks (allow usage before logger is included)
#ifndef DNG_LOG_FATAL
#define DNG_LOG_FATAL(category, fmt, ...) ((void)0)
#endif
#ifndef DNG_LOG_ERROR
#define DNG_LOG_ERROR(category, fmt, ...) ((void)0)
#endif

namespace dng::core {

    // Runtime policy takes precedence when available; falls back to compile-time gate.
    // ---
    // Purpose : Determine whether the current OOM policy requires termination.
    // Contract: Reads compile-time flag and optional runtime config; never allocates.
    // Notes   : constexpr-friendly to allow compile-time evaluation in static paths.
    // ---
    inline bool ShouldFatalOnOOM() noexcept {
        if constexpr (CompiledFatalOnOOM()) {
            return MemoryConfig::GetGlobal().fatal_on_oom;
        }
        else {
            return CompiledFatalOnOOM();
        }
    }

    // ---
    // Purpose : Execute the fatal OOM path, logging context before terminating.
    // Contract: Never returns; safe to call with null `where`; uses std::abort for termination.
    // Notes   : Logging category defined via DNG_MEM_LOG_CATEGORY and may be a stub early in startup.
    // ---
    [[noreturn]] inline void FatalOOM(usize size, usize align,
        const char* where,
        const char* file, int line) noexcept
    {
        DNG_LOG_FATAL(DNG_MEM_LOG_CATEGORY,
            "Out of memory in %s: size=%zu align=%zu at %s:%d",
            where ? where : "<unknown>", static_cast<size_t>(size),
            static_cast<size_t>(align), file, line);

        // Immediate termination (can be adapted per platform if needed).
        std::abort();
    }

    // ---
    // Purpose : Report a recoverable OOM while allowing the caller to continue.
    // Contract: Logs at error severity when verbosity threshold permits; does not throw.
    // Notes   : All parameters preserved for diagnostics even when logging disabled.
    // ---
    inline void ReportOOM(usize size, usize align,
        const char* where,
        const char* file, int line) noexcept
    {
#if DNG_MEM_LOG_VERBOSITY >= 1
        DNG_LOG_ERROR(DNG_MEM_LOG_CATEGORY,
            "Allocation failed in %s: size=%zu align=%zu at %s:%d",
            where ? where : "<unknown>", static_cast<size_t>(size),
            static_cast<size_t>(align), file, line);
#else
        (void)size; (void)align; (void)where; (void)file; (void)line;
#endif
    }

    // ---
    // Purpose : Route allocation failures to fatal or non-fatal handler based on policy.
    // Contract: Strongly noexcept; `where` may be null; never reallocates.
    // Notes   : Central entry used by the DNG_MEM_CHECK_OOM macro.
    // ---
    inline void OnAllocFailure(usize size, usize align,
        const char* where, const char* file, int line) noexcept
    {
        if (ShouldFatalOnOOM()) {
            FatalOOM(size, align, where, file, line);
        }
        else {
            ReportOOM(size, align, where, file, line);
            // Non-fatal mode: the caller will continue with a null pointer.
        }
    }

} // namespace dng::core

// ---
// Purpose : Convenience macro for invoking OOM policy after allocation failure.
// Contract: Evaluate each argument exactly once; safe in constexpr contexts only when unused.
// Notes   : Wraps OnAllocFailure with file/line capture for diagnostics.
// ---
#define DNG_MEM_CHECK_OOM(size, align, where) \
    do { ::dng::core::OnAllocFailure((size), (align), (where), __FILE__, __LINE__); } while(0)
