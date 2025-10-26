#pragma once
#include "Core/CoreMinimal.hpp"  // Logger + Types + Platform
#include "MemoryConfig.hpp" // compile-time defaults + runtime cfg
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
    inline bool ShouldFatalOnOOM() noexcept {
        if constexpr (CompiledFatalOnOOM()) {
            return MemoryConfig::GetGlobal().fatal_on_oom;
        }
        else {
            return CompiledFatalOnOOM();
        }
    }

    // Fatal OOM handler: must not return.
    // Logs at fatal level, then forces immediate process termination.
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

    // Non-fatal OOM report: logs and returns to the caller (who will see nullptr).
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

    // Dispatch OOM handling according to policy: fatal or non-fatal.
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

// Always invoke this macro AFTER detecting an allocation failure (nullptr).
// It delegates to the runtime policy (fatal vs non-fatal) via OnAllocFailure().
#define DNG_MEM_CHECK_OOM(size, align, where) \
    do { ::dng::core::OnAllocFailure((size), (align), (where), __FILE__, __LINE__); } while(0)
