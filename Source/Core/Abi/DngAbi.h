// ============================================================================
// D-Engine - Core/Abi/DngAbi.h
// ----------------------------------------------------------------------------
// Purpose : Common ABI definitions for cross-language modules (C99-compatible).
// Contract: POD-only types, explicit sizes; no exceptions/RTTI/unwinding; C ABI
//           with explicit calling/export macros; thread-safety and ownership
//           defined by higher-level APIs; ASCII-only.
// Notes   : ABI v1 is frozen once published. Do not modify existing v1 entries.
//           The `abi_version` field in dng_abi_header_v1 is interpreted by the
//           containing contract; not every table in this codebase uses `v1`.
// ============================================================================
#ifndef DNG_ABI_DNG_ABI_H
#define DNG_ABI_DNG_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "Core/Platform/PlatformCAbi.h"

// Platform exports and calling convention ------------------------------------------------
#define DNG_ABI_CALL DNG_CABI_CALL
#define DNG_ABI_API  DNG_CABI_API

// Fixed-width primitive aliases -----------------------------------------------------------
typedef uint8_t  dng_u8;
typedef uint16_t dng_u16;
typedef uint32_t dng_u32;
typedef uint64_t dng_u64;
typedef int32_t  dng_i32;
typedef float    dng_f32;

enum { DNG_ABI_VERSION_V1 = 1u };

typedef struct dng_abi_header_v1 {
    dng_u32 struct_size; // Caller sets to sizeof(the containing struct) before use.
    dng_u32 abi_version; // Version tag defined by the containing ABI contract.
} dng_abi_header_v1;

typedef dng_u32 dng_status_v1;

#define DNG_STATUS_OK            ((dng_status_v1)0u)
#define DNG_STATUS_FAIL          ((dng_status_v1)1u)
#define DNG_STATUS_INVALID_ARG   ((dng_status_v1)2u)
#define DNG_STATUS_OUT_OF_MEMORY ((dng_status_v1)3u)
#define DNG_STATUS_UNSUPPORTED   ((dng_status_v1)4u)

typedef dng_u8 dng_bool_v1;
#define DNG_BOOL_FALSE ((dng_bool_v1)0u)
#define DNG_BOOL_TRUE  ((dng_bool_v1)1u)

typedef struct dng_str_view_v1 {
    const char* data; // May be NULL only when size == 0; otherwise non-NULL. UTF-8 recommended.
    dng_u32     size; // Byte length; if > 0, data must be non-NULL; no implicit terminator.
} dng_str_view_v1;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_ABI_H
