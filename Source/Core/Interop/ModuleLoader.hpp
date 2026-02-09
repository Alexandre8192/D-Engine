// ============================================================================
// D-Engine - Core/Interop/ModuleLoader.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal cross-platform loader for ABI modules (C ABI v1).
// Contract: No exceptions/RTTI; returns dng_status_v1; ASCII-only messages;
//           ownership of loaded module belongs to ModuleLoader until Unload.
//           ModuleLoader does not invoke module shutdown callbacks; callers
//           must call module_api.shutdown(module_api.window.ctx, host) before
//           Unload when the module exports shutdown.
// Notes   : Dynamic loading is slow/cold-path. Thread-safety is caller-managed.
// ============================================================================
#ifndef DNG_INTEROP_MODULE_LOADER_HPP
#define DNG_INTEROP_MODULE_LOADER_HPP

#include "Core/Abi/DngModuleApi.h"

namespace dng
{

class ModuleLoader
{
public:
    ModuleLoader() noexcept;
    ~ModuleLoader() noexcept;

    // Purpose : Load a shared module and fetch its ABI table.
    // Contract: path/host/outApi must be non-null; returns status; no throw;
    //           leaves loader unloaded on failure.
    dng_status_v1 Load(const char* path, const dng_host_api_v1* host, dng_module_api_v1* outApi) noexcept;

    // Purpose : Unload a previously loaded module.
    // Contract: Safe to call multiple times; no throw.
    void Unload() noexcept;

    // Purpose : Query loaded state.
    // Contract: No throw.
    dng_bool_v1 IsLoaded() const noexcept;

private:
    void Log(const dng_host_api_v1* host, dng_u32 level, const char* message) const noexcept;

    void* m_handle;
};

} // namespace dng

#endif // DNG_INTEROP_MODULE_LOADER_HPP
