// ============================================================================
// D-Engine - Core/Abi/DngModuleApi.h
// ----------------------------------------------------------------------------
// Purpose : Module entrypoint and generic subsystem export catalogue for ABI v1.
// Contract: C99 ABI; structs start with { struct_size, abi_version }; POD-only;
//           functions return dng_status_v1; no exceptions/RTTI/unwinding.
// Notes   : ABI v1 uses a module-level context plus a list of typed interface
//           tables so modules are not biased toward any specific subsystem.
//           Thread-safety/determinism remain defined by host usage. ASCII-only.
// ============================================================================
#ifndef DNG_ABI_DNG_MODULE_API_H
#define DNG_ABI_DNG_MODULE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "DngAbi.h"
#include "DngHostApi.h"

#define DNG_MODULE_INTERFACE_NAME_WINDOW "dng.window"

typedef struct dng_module_interface_v1 {
    dng_str_view_v1          interface_name;    // Stable identifier, e.g. "dng.window"
    dng_u32                  interface_version; // Version of the pointed API table
    const dng_abi_header_v1* api;               // Points to a subsystem table whose first field is dng_abi_header_v1
} dng_module_interface_v1;

typedef struct dng_module_api_v1 {
    dng_abi_header_v1 header; // { struct_size, abi_version }

    dng_str_view_v1 module_name;
    dng_u32         module_version_major;
    dng_u32         module_version_minor;
    dng_u32         module_version_patch;

    void*                          module_ctx;      // Module-owned context passed back to shutdown.
    const dng_module_interface_v1* interfaces;      // Array of exported subsystem tables.
    dng_u32                        interface_count; // Number of valid entries in `interfaces`.

    // Purpose : Shutdown module and free allocated context.
    // Contract: Must be called before module unload; `module_ctx` remains valid
    //           until this is called; returns DNG_STATUS_OK on success or an
    //           error code on failure. The host must call shutdown at most once
    //           for a given context; repeated calls with the same module_ctx are
    //           undefined because the context may already be freed.
    // Notes   : If NULL, module uses static storage and no cleanup is needed.
    dng_status_v1 (DNG_ABI_CALL *shutdown)(void* module_ctx, const dng_host_api_v1* host);
} dng_module_api_v1;

DNG_ABI_API dng_status_v1 DNG_ABI_CALL dngModuleGetApi_v1(
    const dng_host_api_v1* host,
    dng_module_api_v1* out_api);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_MODULE_API_H
