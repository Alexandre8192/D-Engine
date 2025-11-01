#pragma once
// ============================================================================
// D-Engine - Core/Memory/OOM.hpp
// ----------------------------------------------------------------------------
// Purpose : Declare the engine-wide out-of-memory policy helpers invoked by
//           allocators and wrappers after allocation failure. Uses the engine
//           logging front-end; no local fallbacks.
// Contract: All entry points are header-only, constexpr-safe, and noexcept;
//           they neither allocate nor throw and terminate deterministically
//           according to configuration. This header requires the logging
//           front-end via Logger.hpp; no macro redefinition occurs here.
// Notes   : Functions are inline to keep header self-contained. The runtime
//           policy flag is updated by MemorySystem via SetFatalOnOOMPolicy.
//           No hidden allocations occur; soft OOM escalation to std::bad_alloc
//           remains confined to the global new/delete bridge. We avoid shadowing
//           `DNG_LOG_*` to prevent silent diagnostic loss due to include order.
// ============================================================================
#include "Core/Logger.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>

#ifndef DNG_MEM_LOG_CATEGORY
#define DNG_MEM_LOG_CATEGORY "Memory" // Purpose : Default core memory logging category when callers omit an override.
#endif

namespace dng {
namespace core {

#ifndef DNG_MEM_FATAL_ON_OOM
#define DNG_MEM_FATAL_ON_OOM 0
#endif

    namespace detail
    {
        [[nodiscard]] inline std::atomic<bool>& FatalPolicyFlag() noexcept
        {
            static std::atomic<bool> policy{ DNG_MEM_FATAL_ON_OOM != 0 };
            return policy;
        }
    }

    // Runtime policy takes precedence when available; falls back to compile-time gate.
    // ---
    // Purpose : Determine whether the current OOM policy requires termination.
    // Contract: Reads runtime configuration (falls back to compile-time default);
    //           no allocation, no logging. Result is stable until policy changes
    //           through `SetFatalOnOOMPolicy`.
    // Notes   : `true` => Hard/OOM: abort immediately. `false` => Soft/OOM: caller
    //           observes nullptr (only global new/delete translate it to
    //           std::bad_alloc).
    // ---
    [[nodiscard]] inline bool ShouldFatalOnOOM() noexcept
    {
        return detail::FatalPolicyFlag().load(std::memory_order_relaxed);
    }

    // ---
    // Purpose : Convenience helper for Soft/OOM sites (non-terminating).
    // Contract: Pure wrapper around `ShouldFatalOnOOM()`; provided to clarify
    //           intent at call sites without duplicating the negation logic.
    // Notes   : Only global new/delete may escalate Soft/OOM to std::bad_alloc.
    // ---
    [[nodiscard]] inline bool ShouldSurfaceBadAlloc() noexcept
    {
        return !ShouldFatalOnOOM();
    }

    // ---
    // Purpose : Update the runtime OOM disposition (Hard abort vs Soft/nullptr).
    // Contract: Callable from any thread; MemorySystem invokes it after resolving
    //           configuration. Changes take effect immediately for subsequent
    //           allocations. Remains noexcept and lock-free.
    // Notes   : Tests may toggle this directly; production code should go through
    //           MemoryConfig APIs.
    // ---
    inline void SetFatalOnOOMPolicy(bool fatal) noexcept
    {
        detail::FatalPolicyFlag().store(fatal, std::memory_order_relaxed);
    }

    // ---
    // Purpose : Execute the fatal OOM path, logging context before terminating.
    // Contract: Never returns; safe to call with null `where`; uses std::abort for termination.
    // Notes   : Logging category defined via DNG_MEM_LOG_CATEGORY and may be a stub early in startup.
    // ---
    [[noreturn]] inline void FatalOOM(std::size_t size, std::size_t align,
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
    inline void ReportOOM(std::size_t size, std::size_t align,
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
    inline void OnAllocFailure(std::size_t size, std::size_t align,
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

} // namespace core
} // namespace dng

// ---
// Purpose : Convenience macro for invoking OOM policy after allocation failure.
// Contract: Evaluate each argument exactly once; safe in constexpr contexts only when unused.
// Notes   : Wraps OnAllocFailure with file/line capture for diagnostics.
// ---
#define DNG_MEM_CHECK_OOM(size, align, where) \
    do { ::dng::core::OnAllocFailure((size), (align), (where), __FILE__, __LINE__); } while(0)
