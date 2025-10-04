// =============================
// PlatformMacros.hpp
// =============================
#pragma once

// General-purpose lightweight macros safe across platforms.
// Includes: UNUSED, STATIC_ASSERT alias, branch prediction, array length, etc.

// -----------------------------
// UNUSED(x)
// -----------------------------
#ifndef DNG_UNUSED
#define DNG_UNUSED(x) (void)(x)
#endif

// -----------------------------
// Compile-time assertion alias
// -----------------------------
#ifndef DNG_STATIC_ASSERT
#define DNG_STATIC_ASSERT(expr, msg) static_assert((expr), msg)
#endif

// -----------------------------
// Branch prediction hints
// -----------------------------
#if defined(__GNUC__) || defined(__clang__)
#ifndef DNG_LIKELY
#define DNG_LIKELY(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef DNG_UNLIKELY
#define DNG_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
#else
  // Safe fallbacks
#ifndef DNG_LIKELY
#define DNG_LIKELY(x)   (!!(x))
#endif
#ifndef DNG_UNLIKELY
#define DNG_UNLIKELY(x) (!!(x))
#endif
#endif

// -----------------------------
// Array count (do not pass pointers)
// -----------------------------
#ifndef DNG_ARRAY_COUNT
#define DNG_ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// -----------------------------
// Defer helper (simple scope-exit pattern)
// -----------------------------
// Usage: auto _defer = DNG_DEFER([&]{ /* cleanup */; });
#include <utility>
template <typename F>
struct DngDefer {
    F f;
    explicit DngDefer(F&& fn) : f(std::forward<F>(fn)) {}
    ~DngDefer() noexcept { f(); }
};
#define DNG_DEFER(lambda_expr) DngDefer _dng_defer_##__LINE__ { lambda_expr }

// Keep this header minimal; prefer standard attributes (e.g. [[nodiscard]]) over macro aliases.
