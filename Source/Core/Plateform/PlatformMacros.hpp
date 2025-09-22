// =============================
// PlatformMacros.hpp
// =============================
#pragma once

// This file defines general-purpose utility macros that are safe to use across platforms.
// Includes: assertions, branch prediction hints, UNUSED macro, etc.

#include <cassert> // for assert()

// -----------------------------
// UNUSED(x)
// -----------------------------
// Marks a variable as intentionally unused to avoid compiler warnings.
#ifndef DNG_UNUSED
#define DNG_UNUSED(x) (void)(x)
#endif

// -----------------------------
// CHECK(x)
// -----------------------------
// Runtime assertion. Aborts in debug mode if condition is false.
#ifndef DNG_CHECK
#define DNG_CHECK(x) assert(x)
#endif

// -----------------------------
// LIKELY / UNLIKELY
// -----------------------------
// Compiler branch prediction hints (optional, improve performance on hot paths).
#if defined(__GNUC__) || defined(__clang__)
#define DNG_LIKELY(x)   __builtin_expect(!!(x), 1)
#define DNG_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define DNG_LIKELY(x)   (x)
#define DNG_UNLIKELY(x) (x)
#endif

// -----------------------------
// Compile-time assertion alias (C++11+)
// -----------------------------
#ifndef DNG_STATIC_ASSERT
#define DNG_STATIC_ASSERT(expr, msg) static_assert(expr, msg)
#endif

// -----------------------------
// TODO/FIXME tagging (optional)
// -----------------------------
#define DNG_TODO(msg)   static_assert(false, "TODO: " msg)
#define DNG_FIXME(msg)  static_assert(false, "FIXME: " msg)

// Add more lightweight macros here as needed (no runtime overhead, no dependencies)
