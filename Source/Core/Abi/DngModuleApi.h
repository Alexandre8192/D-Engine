// ============================================================================
// D-Engine - Core/Abi/DngModuleApi.h
// ----------------------------------------------------------------------------
// Purpose : Module entrypoint and generic subsystem export catalogue for the
//           supported module ABI v2.
// Contract: C99 ABI; structs start with { struct_size, abi_version }; POD-only;
//           functions return dng_status_v1; no exceptions/RTTI/unwinding.
// Notes   : Module ABI v2 is the current public contract. Earlier module-loading
//           experiments are legacy and intentionally unsupported here.
//           Migration rule: modules must export dngModuleGetApi_v2 and populate
//           dng_module_api_v2. Thread-safety/determinism remain defined by host
//           usage. ASCII-only.
// ============================================================================
#ifndef DNG_ABI_DNG_MODULE_API_H
#define DNG_ABI_DNG_MODULE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "DngAbi.h"
#include "DngHostApi.h"

#define DNG_MODULE_INTERFACE_NAME_WINDOW "dng.window"
#define DNG_MODULE_GET_API_V2_NAME       "dngModuleGetApi_v2"

enum { DNG_MODULE_API_VERSION_V2 = 2u };

// Purpose : Describe one versioned interface exported through the module
//           catalogue.
// Contract: interface_name, interface_version, and api identify one stable
//           exported table. The interface name string and pointed API table
//           must remain valid until module unload.
typedef struct dng_module_interface_v1 {
    dng_str_view_v1          interface_name;    // Stable identifier, e.g. "dng.window"
    dng_u32                  interface_version; // Version of the pointed API table
    const dng_abi_header_v1* api;               // Exported table whose first field is dng_abi_header_v1
} dng_module_interface_v1;

// Purpose : Describe one loaded module instance and its exported interface
//           catalogue.
// Contract: The caller owns the top-level dng_module_api_v2 storage passed to
//           dngModuleGetApi_v2. The module owns any pointed data referenced by
//           that table. module_name.data, interfaces, each exported interface
//           name string, each exported interface payload pointer, and module_ctx
//           when non-null must remain valid until module unload. If shutdown is
//           non-null, module_ctx must be non-null and remain valid until
//           shutdown returns. If shutdown is null, module_ctx may be null or
//           may reference storage that requires no explicit shutdown before
//           module unload.
typedef struct dng_module_api_v2 {
    dng_abi_header_v1 header; // { struct_size, abi_version }

    dng_str_view_v1 module_name;          // Module-owned identifier string.
    dng_u32         module_version_major; // Semantic version triplet for diagnostics/policy.
    dng_u32         module_version_minor;
    dng_u32         module_version_patch;

    void*                          module_ctx;      // Optional module-owned context. If shutdown == NULL, may be NULL or may reference storage that requires no explicit shutdown before unload.
    const dng_module_interface_v1* interfaces;      // Module-owned array of exported subsystem tables.
    dng_u32                        interface_count; // Number of valid entries in interfaces.

    // Purpose : Shutdown module-owned state before module unload.
    // Contract: Optional. If non-null, the host must call shutdown exactly once
    //           before module unload and must pass the exported non-null
    //           module_ctx. If null, the module must not require any explicit
    //           shutdown call before unload.
    dng_status_v1 (DNG_ABI_CALL *shutdown)(void* module_ctx, const dng_host_api_v1* host);
} dng_module_api_v2;

// Purpose : Populate caller-owned dng_module_api_v2 storage for one module
//           instance.
// Contract: host/out_api must be non-null. The caller should pass either a
//           zeroed out_api or one whose header is initialized with
//           { struct_size = sizeof(dng_module_api_v2),
//             abi_version = DNG_MODULE_API_VERSION_V2 }.
DNG_ABI_API dng_status_v1 DNG_ABI_CALL dngModuleGetApi_v2(
    const dng_host_api_v1* host,
    dng_module_api_v2* out_api);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DNG_ABI_DNG_MODULE_API_H
