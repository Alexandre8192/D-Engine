// =============================
// PlatformDefines.hpp
// =============================
#pragma once

// Pure preprocessor platform detection (OS, arch, word size, endianness).
// Keep this header *very* lightweight: no runtime logic, no external deps.

// -----------------------------
// OS Detection
// -----------------------------
#if defined(_WIN32) || defined(_WIN64)
#define DNG_PLATFORM_WINDOWS 1
#else
#define DNG_PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
#define DNG_PLATFORM_LINUX 1
#else
#define DNG_PLATFORM_LINUX 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define DNG_PLATFORM_APPLE 1
#else
#define DNG_PLATFORM_APPLE 0
#endif

// Add other OSes later: Android/iOS/Consoles/Web..

// -----------------------------
// CPU Architecture
// -----------------------------
#if defined(_M_X64) || defined(__x86_64__)
#define DNG_CPU_X64 1
#else
#define DNG_CPU_X64 0
#endif

#if defined(_M_IX86) || defined(__i386__)
#define DNG_CPU_X86 1
#else
#define DNG_CPU_X86 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define DNG_CPU_ARM64 1
#else
#define DNG_CPU_ARM64 0
#endif

#if defined(__arm__) && !defined(__aarch64__)
#define DNG_CPU_ARM32 1
#else
#define DNG_CPU_ARM32 0
#endif

// -----------------------------
// Word size (32/64 bits)
// -----------------------------
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__) || defined(__LP64__)
#define DNG_PLATFORM_64BITS 1
#define DNG_PLATFORM_32BITS 0
#else
#define DNG_PLATFORM_64BITS 0
#define DNG_PLATFORM_32BITS 1
#endif

// -----------------------------
// Endianness
// -----------------------------
// Most gaming targets are little-endian, but still detect explicitly.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define DNG_PLATFORM_LITTLE_ENDIAN 1
#define DNG_PLATFORM_BIG_ENDIAN    0
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define DNG_PLATFORM_LITTLE_ENDIAN 0
#define DNG_PLATFORM_BIG_ENDIAN    1
#elif DNG_PLATFORM_WINDOWS
  // Windows targets are little-endian on all supported archs
#define DNG_PLATFORM_LITTLE_ENDIAN 1
#define DNG_PLATFORM_BIG_ENDIAN    0
#else
  // Fallback: assume little-endian (common), but allow projects to override
#ifndef DNG_PLATFORM_LITTLE_ENDIAN
#define DNG_PLATFORM_LITTLE_ENDIAN 1
#endif
#ifndef DNG_PLATFORM_BIG_ENDIAN
#define DNG_PLATFORM_BIG_ENDIAN    0
#endif
#endif

// -----------------------------
// Composite flags
// -----------------------------
#define DNG_PLATFORM_DESKTOP (DNG_PLATFORM_WINDOWS || DNG_PLATFORM_LINUX || DNG_PLATFORM_APPLE)
#define DNG_PLATFORM_UNKNOWN (!DNG_PLATFORM_WINDOWS && !DNG_PLATFORM_LINUX && !DNG_PLATFORM_APPLE)

// -----------------------------
// Sanity guards
// -----------------------------
#if !defined(DNG_PLATFORM_32BITS) || !defined(DNG_PLATFORM_64BITS)
#error "DNG: Must define DNG_PLATFORM_32BITS and DNG_PLATFORM_64BITS."
#endif

#if ((DNG_PLATFORM_32BITS + DNG_PLATFORM_64BITS) != 1)
#error "DNG: Exactly one of DNG_PLATFORM_32BITS or DNG_PLATFORM_64BITS must be 1."
#endif

#if ((DNG_PLATFORM_LITTLE_ENDIAN + DNG_PLATFORM_BIG_ENDIAN) != 1)
#error "DNG: Exactly one of DNG_PLATFORM_LITTLE_ENDIAN or DNG_PLATFORM_BIG_ENDIAN must be 1."
#endif

// Keep this file preprocessor-only.
