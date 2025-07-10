// =============================
// PlatformDefines.hpp
// =============================
#pragma once

// This file detects the target OS and CPU architecture.
// It defines basic preprocessor macros for platform-specific compilation.
// Should be included very early to enable conditional compilation in platform-dependent files.

// -----------------------------
// OS Detection
// -----------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define DNG_PLATFORM_WINDOWS 1
#else
    #define DNG_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__)
    #define DNG_PLATFORM_APPLE 1
#else
    #define DNG_PLATFORM_APPLE 0
#endif

#if defined(__linux__)
    #define DNG_PLATFORM_LINUX 1
#else
    #define DNG_PLATFORM_LINUX 0
#endif

#if defined(__unix__)
    #define DNG_PLATFORM_UNIX 1
#else
    #define DNG_PLATFORM_UNIX 0
#endif

// -----------------------------
// CPU Architecture Detection
// -----------------------------
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    #define DNG_PLATFORM_64BITS 1
#else
    #define DNG_PLATFORM_64BITS 0
#endif

#if defined(_M_IX86) || defined(__i386__)
    #define DNG_PLATFORM_32BITS 1
#else
    #define DNG_PLATFORM_32BITS (!DNG_PLATFORM_64BITS)
#endif

// -----------------------------
// Optional composite flags
// -----------------------------
#define DNG_PLATFORM_DESKTOP (DNG_PLATFORM_WINDOWS || DNG_PLATFORM_LINUX || DNG_PLATFORM_APPLE)
#define DNG_PLATFORM_UNKNOWN (!DNG_PLATFORM_WINDOWS && !DNG_PLATFORM_LINUX && !DNG_PLATFORM_APPLE)

// Future additions could include: Android, iOS, Console, WebAssembly...

// Note: No runtime logic, types or includes should exist in this file.
// This file should be pure preprocessor logic only.
