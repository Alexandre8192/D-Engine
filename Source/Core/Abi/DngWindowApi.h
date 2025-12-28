// ============================================================================
// D-Engine - Core/Abi/DngWindowApi.h
// ----------------------------------------------------------------------------
// Purpose : Window subsystem ABI (v1) using C99 POD types and function tables.
// Contract: C ABI, POD-only; functions return dng_status_v1; no exceptions or
//           RTTI; ownership is explicit; host must not touch ctx internals.
// Notes   : ABI v1 is frozen once published. Thread-safety is defined by the
//           host embedding. Determinism depends on host event pump cadence.
// ============================================================================
#ifndef DNG_ABI_DNG_WINDOW_API_H
#define DNG_ABI_DNG_WINDOW_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Core/Abi/DngAbi.h"

typedef dng_u64 dng_window_handle_v1; // 0 is invalid.

typedef struct dng_window_desc_v1 {
    dng_u32         width;
    dng_u32         height;
    dng_str_view_v1 title;  // Non-owning view.
    dng_u32         flags;  // Reserved; must be 0 for v1.
} dng_window_desc_v1;

typedef struct dng_window_size_v1 {
    dng_u32 width;
    dng_u32 height;
} dng_window_size_v1;

typedef struct dng_window_api_v1 {
    dng_abi_header_v1 header; // { struct_size, abi_version }
    void*             ctx;    // Module-owned context; host must not mutate.

    dng_status_v1 (*create)(void* ctx, const dng_window_desc_v1* desc, dng_window_handle_v1* out_handle);
    dng_status_v1 (*destroy)(void* ctx, dng_window_handle_v1 handle);
    dng_status_v1 (*poll)(void* ctx);
    dng_status_v1 (*get_size)(void* ctx, dng_window_handle_v1 handle, dng_window_size_v1* out_size);
    dng_status_v1 (*set_title)(void* ctx, dng_window_handle_v1 handle, dng_str_view_v1 title);
} dng_window_api_v1;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_WINDOW_API_H
