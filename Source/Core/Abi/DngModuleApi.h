// ============================================================================
// D-Engine - Core/Abi/DngModuleApi.h
// ----------------------------------------------------------------------------
// Purpose : Module entrypoint and aggregated subsystem tables for ABI v1.
// Contract: C99 ABI; structs start with { struct_size, abi_version }; POD-only;
//           functions return dng_status_v1; no exceptions/RTTI/unwinding.
// Notes   : ABI v1 is frozen once published. Thread-safety/determinism defined
//           by host usage. ASCII-only.
// ============================================================================
#ifndef DNG_ABI_DNG_MODULE_API_H
#define DNG_ABI_DNG_MODULE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Core/Abi/DngAbi.h"
#include "Core/Abi/DngHostApi.h"
#include "Core/Abi/DngWindowApi.h"

typedef struct dng_module_api_v1 {
    dng_abi_header_v1 header; // { struct_size, abi_version }

    dng_str_view_v1 module_name;
    dng_u32         module_version_major;
    dng_u32         module_version_minor;
    dng_u32         module_version_patch;

    dng_window_api_v1 window; // Pilot subsystem export.

    // Purpose : Shutdown module and free allocated context.
    // Contract: Must be called before module unload; ctx from window.ctx must
    //           remain valid until this is called; no exceptions; idempotent.
    // Notes   : Added to support proper cleanup of dynamically allocated contexts.
    //           If NULL, module uses static storage and no cleanup is needed.
    void (*shutdown)(void* ctx, const dng_host_api_v1* host);
} dng_module_api_v1;

DNG_ABI_API dng_status_v1 DNG_ABI_CALL dngModuleGetApi_v1(
    const dng_host_api_v1* host,
    dng_module_api_v1* out_api);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_MODULE_API_H
