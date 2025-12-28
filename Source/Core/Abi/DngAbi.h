// ============================================================================
// D-Engine - Core/Abi/DngAbi.h
// ----------------------------------------------------------------------------
// Purpose : Common ABI definitions for cross-language modules (C99-compatible).
// Contract: POD-only types, explicit sizes; no exceptions/RTTI/unwinding; C ABI
//           with explicit calling/export macros; thread-safety and ownership
//           defined by higher-level APIs; ASCII-only.
// Notes   : ABI v1 is frozen once published. Do not modify existing v1 entries.
// ============================================================================
#ifndef DNG_ABI_DNG_ABI_H
#define DNG_ABI_DNG_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Platform exports and calling convention ------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define DNG_ABI_CALL __cdecl
    #if defined(DNG_ABI_EXPORTS)
        #define DNG_ABI_API __declspec(dllexport)
    #else
        #define DNG_ABI_API __declspec(dllimport)
    #endif
#else
    #define DNG_ABI_CALL
    #if defined(__GNUC__) || defined(__clang__)
        #define DNG_ABI_API __attribute__((visibility("default")))
    #else
        #define DNG_ABI_API
    #endif
#endif

// Fixed-width primitive aliases -----------------------------------------------------------
typedef uint8_t  dng_u8;
typedef uint16_t dng_u16;
typedef uint32_t dng_u32;
typedef uint64_t dng_u64;
typedef int32_t  dng_i32;
typedef float    dng_f32;

enum { DNG_ABI_VERSION_V1 = 1u };

typedef struct dng_abi_header_v1 {
    dng_u32 struct_size;
    dng_u32 abi_version;
} dng_abi_header_v1;

typedef dng_u32 dng_status_v1;

#define DNG_STATUS_OK            ((dng_status_v1)0u)
#define DNG_STATUS_FAIL          ((dng_status_v1)1u)
#define DNG_STATUS_INVALID_ARG   ((dng_status_v1)2u)
#define DNG_STATUS_OUT_OF_MEMORY ((dng_status_v1)3u)
#define DNG_STATUS_UNSUPPORTED   ((dng_status_v1)4u)

typedef dng_u8 dng_bool_v1;

typedef struct dng_str_view_v1 {
    const char* data;
    dng_u32     size;
} dng_str_view_v1;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_ABI_H
