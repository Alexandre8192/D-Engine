// =============================
// PlatformCompiler.hpp
// =============================
#pragma once

// This file detects the compiler and defines helper macros accordingly.
// It provides FORCEINLINE, DLLEXPORT, alignment macros, and other compiler-specific attributes.

// -----------------------------
// Compiler Detection
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

#if defined(__GNUC__) && !defined(__clang__)
    #define DNG_COMPILER_GCC 1
#else
    #define DNG_COMPILER_GCC 0
#endif

// -----------------------------
// FORCEINLINE / NOINLINE
// -----------------------------
#if DNG_COMPILER_MSVC
    #define DNG_FORCEINLINE    __forceinline
    #define DNG_FORCENOINLINE  __declspec(noinline)
#elif DNG_COMPILER_CLANG || DNG_COMPILER_GCC
    #define DNG_FORCEINLINE    inline __attribute__((always_inline))
    #define DNG_FORCENOINLINE  __attribute__((noinline))
#else
    #define DNG_FORCEINLINE    inline
    #define DNG_FORCENOINLINE
#endif

// -----------------------------
// DLL Import / Export
// -----------------------------
#if DNG_COMPILER_MSVC
    #define DNG_DLLIMPORT __declspec(dllimport)
    #define DNG_DLLEXPORT __declspec(dllexport)
#else
    #define DNG_DLLIMPORT __attribute__((visibility("default")))
    #define DNG_DLLEXPORT __attribute__((visibility("default")))
#endif

// -----------------------------
// Alignment Macro
// -----------------------------
#if DNG_COMPILER_MSVC
    #define DNG_ALIGNAS(n) __declspec(align(n))
#else
    #define DNG_ALIGNAS(n) __attribute__((aligned(n)))
#endif

// -----------------------------
// Restrict Keyword
// -----------------------------
#if DNG_COMPILER_MSVC
    #define DNG_RESTRICT __restrict
#else
    #define DNG_RESTRICT __restrict__
#endif

// -----------------------------
// Nullptr and other constants
// -----------------------------
#ifndef DNG_NULL
    #define DNG_NULL nullptr
#endif