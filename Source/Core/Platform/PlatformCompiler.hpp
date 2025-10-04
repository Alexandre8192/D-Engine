// =============================
// PlatformCompiler.hpp
// =============================
#pragma once

// Compiler/attributes/visibility helpers.
// Keep simple. Prefer standard C++ attributes where possible.

// -----------------------------
// Compiler detection
// -----------------------------
#if defined(_MSC_VER)
#define DNG_COMPILER_MSVC 1
#else
#define DNG_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#define DNG_COMPILER_CLANG 1
#else
#define DNG_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !DNG_COMPILER_CLANG
#define DNG_COMPILER_GCC 1
#else
#define DNG_COMPILER_GCC 0
#endif

// -----------------------------
// Version helpers (optional)
// -----------------------------
#if DNG_COMPILER_MSVC
#define DNG_MSVC_VER _MSC_VER
#endif
#if DNG_COMPILER_CLANG
#define DNG_CLANG_MAJOR __clang_major__
#define DNG_CLANG_MINOR __clang_minor__
#endif
#if DNG_COMPILER_GCC
#define DNG_GCC_MAJOR __GNUC__
#define DNG_GCC_MINOR __GNUC_MINOR__
#endif

// -----------------------------
// Inlining / noinline
// -----------------------------
#if DNG_COMPILER_MSVC
#define DNG_FORCEINLINE __forceinline
#define DNG_NOINLINE    __declspec(noinline)
#else
#define DNG_FORCEINLINE inline __attribute__((always_inline))
#define DNG_NOINLINE    __attribute__((noinline))
#endif

// -----------------------------
// Symbol visibility (DLL/DSO)
// -----------------------------
// Use these *base* macros to build per-module APIs (e.g., DNG_CORE_API).
#if DNG_COMPILER_MSVC
#define DNG_DLLIMPORT __declspec(dllimport)
#define DNG_DLLEXPORT __declspec(dllexport)
#define DNG_VISIBLE
#else
#define DNG_DLLIMPORT
#define DNG_DLLEXPORT __attribute__((visibility("default")))
#define DNG_VISIBLE   __attribute__((visibility("default")))
#endif

// Example (to be put in each module):
// #if defined(DNG_CORE_BUILD)
//   #define DNG_CORE_API DNG_DLLEXPORT
// #else
//   #define DNG_CORE_API DNG_DLLIMPORT
// #endif

// -----------------------------
// Restrict keyword
// -----------------------------
#if DNG_COMPILER_MSVC
#define DNG_RESTRICT __restrict
#else
#define DNG_RESTRICT __restrict__
#endif

// -----------------------------
// Assumptions
// -----------------------------
#if DNG_COMPILER_MSVC
#define DNG_ASSUME(x) __assume(x)
#else
  // Hint to optimizer for impossible paths
#define DNG_ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#endif

// -----------------------------
// Null
// -----------------------------
#ifndef DNG_NULL
#define DNG_NULL nullptr
#endif
