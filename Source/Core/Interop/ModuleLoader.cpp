// ============================================================================
// D-Engine - Core/Interop/ModuleLoader.cpp
// ----------------------------------------------------------------------------
// Purpose : Implementation of minimal cross-platform ABI module loader.
// Contract: No exceptions/RTTI; C ABI entrypoint; returns status codes only;
//           cold-path usage; ASCII-only logging via host log callback.
// Notes   : Caller manages thread-safety. Loader owns the module handle until
//           Unload. ABI v1 entrypoint name is dngModuleGetApi_v1.
// ============================================================================
#include "Core/Interop/ModuleLoader.hpp"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <stdio.h>
#else
    #include <dlfcn.h>
#endif

#include <string.h>

namespace dng
{
namespace
{
    static dng_u32 StrLen32(const char* cstr) noexcept
    {
        if (!cstr)
        {
            return 0;
        }
        const size_t len = ::strlen(cstr);
        return len > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<dng_u32>(len);
    }

    static void LogIssue(const dng_host_api_v1* host, const char* message) noexcept
    {
        if (!host || !host->log || !message)
        {
            return;
        }

        dng_str_view_v1 view;
        view.data = message;
        view.size = StrLen32(message);
        host->log(host->user, 1u, view);
    }

    static dng_status_v1 ValidateStrView(const dng_str_view_v1& view, const char* label, const dng_host_api_v1* host) noexcept
    {
        if (view.size == 0u)
        {
            return DNG_STATUS_OK;
        }

        if (view.data == nullptr)
        {
            LogIssue(host, label);
            return DNG_STATUS_INVALID_ARG;
        }

        return DNG_STATUS_OK;
    }

    static dng_status_v1 ValidateHostApiV1(const dng_host_api_v1* host) noexcept
    {
        if (!host)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        if (host->header.struct_size != sizeof(dng_host_api_v1))
        {
            LogIssue(host, "HostApi struct_size mismatch");
            return DNG_STATUS_INVALID_ARG;
        }

        if (host->header.abi_version != DNG_ABI_VERSION_V1)
        {
            LogIssue(host, "HostApi abi_version mismatch");
            return DNG_STATUS_UNSUPPORTED;
        }

        if (!host->alloc || !host->free)
        {
            LogIssue(host, "HostApi missing alloc/free");
            return DNG_STATUS_INVALID_ARG;
        }

        return DNG_STATUS_OK;
    }

    static dng_status_v1 ValidateWindowApiV1(const dng_window_api_v1* api, const dng_host_api_v1* host) noexcept
    {
        if (!api)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->header.struct_size != sizeof(dng_window_api_v1))
        {
            LogIssue(host, "WindowApi struct_size mismatch");
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->header.abi_version != DNG_ABI_VERSION_V1)
        {
            LogIssue(host, "WindowApi abi_version mismatch");
            return DNG_STATUS_UNSUPPORTED;
        }

        if (!api->ctx)
        {
            LogIssue(host, "WindowApi ctx is null");
            return DNG_STATUS_INVALID_ARG;
        }

        if (!api->create || !api->destroy || !api->poll || !api->get_size || !api->set_title)
        {
            LogIssue(host, "WindowApi missing function pointer");
            return DNG_STATUS_INVALID_ARG;
        }

        return DNG_STATUS_OK;
    }

    static dng_status_v1 ValidateModuleApiV1(const dng_module_api_v1* api, const dng_host_api_v1* host) noexcept
    {
        if (!api)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->header.struct_size != sizeof(dng_module_api_v1))
        {
            LogIssue(host, "ModuleApi struct_size mismatch");
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->header.abi_version != DNG_ABI_VERSION_V1)
        {
            LogIssue(host, "ModuleApi abi_version mismatch");
            return DNG_STATUS_UNSUPPORTED;
        }

        if (ValidateStrView(api->module_name, "ModuleApi module_name invalid", host) != DNG_STATUS_OK)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        return ValidateWindowApiV1(&api->window, host);
    }

#if defined(_WIN32) || defined(_WIN64)
    static void LogWin32Error(const dng_host_api_v1* host, dng_u32 level, const char* prefix) noexcept
    {
        if (!host || !host->log || !prefix)
        {
            return;
        }

        const DWORD err = ::GetLastError();
        char buf[128];
        const int n = ::snprintf(buf, sizeof(buf), "%s (err=%lu)", prefix, (unsigned long)err);
        (void)n;

        dng_str_view_v1 view;
        view.data = buf;
        view.size = StrLen32(buf);
        host->log(host->user, level, view);
    }
#endif
} // namespace

ModuleLoader::ModuleLoader() noexcept
    : m_handle(nullptr)
{
}

ModuleLoader::~ModuleLoader() noexcept
{
    Unload();
}

void ModuleLoader::Log(const dng_host_api_v1* host, dng_u32 level, const char* message) const noexcept
{
    if (host && host->log && message)
    {
        dng_str_view_v1 view;
        view.data = message;
        view.size = StrLen32(message);
        host->log(host->user, level, view);
    }
}

dng_status_v1 ModuleLoader::Load(const char* path, const dng_host_api_v1* host, dng_module_api_v1* outApi) noexcept
{
    if (!path || !host || !outApi)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    const dng_status_v1 host_ok = ValidateHostApiV1(host);
    if (host_ok != DNG_STATUS_OK)
    {
        return host_ok;
    }

    if (m_handle)
    {
        Unload();
    }

#if defined(_WIN32) || defined(_WIN64)
    HMODULE lib = ::LoadLibraryA(path);
    if (!lib)
    {
        LogWin32Error(host, 1u, "LoadLibraryA failed");
        return DNG_STATUS_FAIL;
    }

    FARPROC proc = ::GetProcAddress(lib, "dngModuleGetApi_v1");
    if (!proc)
    {
        // Optional x86 fallback: some toolchains decorate __cdecl exports with a leading underscore.
        proc = ::GetProcAddress(lib, "_dngModuleGetApi_v1");
    }

    if (!proc)
    {
        Log(host, 1u, "dngModuleGetApi_v1 not found");
        ::FreeLibrary(lib);
        return DNG_STATUS_UNSUPPORTED;
    }

    auto entry = reinterpret_cast<dng_status_v1 (DNG_ABI_CALL *)(const dng_host_api_v1*, dng_module_api_v1*)>(proc);
#else
    // RTLD_NOW resolves relocations at load time; RTLD_LOCAL avoids exporting symbols globally.
    void* lib = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        const char* err = ::dlerror();
        Log(host, 1u, err ? err : "dlopen failed");
        return DNG_STATUS_FAIL;
    }

    // Clear any prior error before calling dlsym.
    (void)::dlerror();
    void* sym = ::dlsym(lib, "dngModuleGetApi_v1");
    const char* sym_err = ::dlerror();
    if (sym_err != nullptr)
    {
        Log(host, 1u, sym_err);
        ::dlclose(lib);
        return DNG_STATUS_UNSUPPORTED;
    }
    if (!sym)
    {
        Log(host, 1u, "dlsym returned null for dngModuleGetApi_v1");
        ::dlclose(lib);
        return DNG_STATUS_UNSUPPORTED;
    }

    auto entry = reinterpret_cast<dng_status_v1 (DNG_ABI_CALL *)(const dng_host_api_v1*, dng_module_api_v1*)>(sym);
#endif

    // Zero output before fill and provide header defaults (caller-owned size/version handshake).
    ::memset(outApi, 0, sizeof(*outApi));
    outApi->header.struct_size = sizeof(*outApi);
    outApi->header.abi_version = DNG_ABI_VERSION_V1;
    outApi->window.header.struct_size = sizeof(outApi->window);
    outApi->window.header.abi_version = DNG_ABI_VERSION_V1;

    const dng_status_v1 status = entry(host, outApi);
    if (status != DNG_STATUS_OK)
    {
#if defined(_WIN32) || defined(_WIN64)
        ::FreeLibrary(lib);
#else
        ::dlclose(lib);
#endif
        return status;
    }

    const dng_status_v1 api_ok = ValidateModuleApiV1(outApi, host);
    if (api_ok != DNG_STATUS_OK)
    {
        Log(host, 1u, "Module returned an invalid API table");
#if defined(_WIN32) || defined(_WIN64)
        ::FreeLibrary(lib);
#else
        ::dlclose(lib);
#endif
        ::memset(outApi, 0, sizeof(*outApi));
        return api_ok;
    }

    m_handle = lib;
    return DNG_STATUS_OK;
}

void ModuleLoader::Unload() noexcept
{
    if (!m_handle)
    {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    ::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
    ::dlclose(m_handle);
#endif
    m_handle = nullptr;
}

dng_bool_v1 ModuleLoader::IsLoaded() const noexcept
{
    return m_handle ? 1u : 0u;
}

} // namespace dng
