// ============================================================================
// D-Engine - Core/Abi/DngHostApi.h
// ----------------------------------------------------------------------------
// Purpose : Host-side services exposed to ABI modules (logging and allocation).
// Contract: C99 POD-only; functions return dng_status_v1 or void; no exceptions
//           or RTTI; ownership is explicit (who allocates frees); ASCII-only.
// Notes   : Header-first; ABI v1 is frozen once published. Thread-safety and
//           determinism are defined by the embedding host.
// ============================================================================
#ifndef DNG_ABI_DNG_HOST_API_H
#define DNG_ABI_DNG_HOST_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Core/Abi/DngAbi.h"

typedef struct dng_host_api_v1 {
    dng_abi_header_v1 header;  // { struct_size, abi_version }
    void*             user;    // Non-owning user pointer provided by the host.

    // Purpose : Log a message with a host-defined level.
    // Contract: Never throws; msg is a non-owning view; level is host-defined.
    void (*log)(void* user, dng_u32 level, dng_str_view_v1 msg);

    // Purpose : Allocate memory using host allocator.
    // Contract: Returns aligned block or NULL; caller owns result and must free
    //           with matching size/align via free; no exceptions.
    void* (*alloc)(void* user, dng_u64 size, dng_u64 align);

    // Purpose : Free memory previously allocated via alloc.
    // Contract: ptr/size/align must match alloc call; behavior is undefined
    //           otherwise. Never throws.
    void (*free)(void* user, void* ptr, dng_u64 size, dng_u64 align);
} dng_host_api_v1;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_HOST_API_H
