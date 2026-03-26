// ============================================================================
// D-Engine - Core/Platform/PlatformCAbi.h
// ----------------------------------------------------------------------------
// Purpose : C-compatible platform ABI macros shared by exported C interfaces.
// Contract: Preprocessor-only; safe for both C and C++; no runtime logic.
// Notes   : Keep this file minimal so ABI headers can include it directly.
// ============================================================================
#ifndef DNG_PLATFORM_C_ABI_H
#define DNG_PLATFORM_C_ABI_H

#if defined(_WIN32) || defined(_WIN64)
    #define DNG_CABI_CALL __cdecl
    #if defined(DNG_CABI_EXPORTS) || defined(DNG_ABI_EXPORTS)
        #define DNG_CABI_API __declspec(dllexport)
    #else
        #define DNG_CABI_API __declspec(dllimport)
    #endif
#else
    #define DNG_CABI_CALL
    #if defined(__GNUC__) || defined(__clang__)
        #define DNG_CABI_API __attribute__((visibility("default")))
    #else
        #define DNG_CABI_API
    #endif
#endif

#endif // DNG_PLATFORM_C_ABI_H
