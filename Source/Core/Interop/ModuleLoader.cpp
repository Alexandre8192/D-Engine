// ============================================================================
// D-Engine - Core/Interop/ModuleLoader.cpp
// ----------------------------------------------------------------------------
// Purpose : Implementation of minimal cross-platform ABI module loader.
// Contract: No exceptions/RTTI; C ABI entrypoint; returns status codes only;
//           cold-path usage; ASCII-only logging via host log callback.
// Notes   : Caller manages thread-safety. Loader owns the module handle until
//           Unload. The loader validates only generic module catalogue shape;
//           subsystem-specific table validation belongs to typed interop helpers.
// ============================================================================
#include "Core/Interop/ModuleLoader.hpp"
#include "Core/Platform/PlatformDefines.hpp"

#if DNG_PLATFORM_WINDOWS
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
    static constexpr dng_u64 kModuleApiScratchCanary = 0xD6E74F135A91C2B7ull;
    static constexpr dng_u32 kModuleApiScratchCanaryCount = 8u;

    struct ModuleApiScratchBufferV2
    {
        dng_module_api_v2 api;
        dng_u64           tail_canary[kModuleApiScratchCanaryCount];
    };

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

    static bool StrViewEquals(const dng_str_view_v1& lhs, const dng_str_view_v1& rhs) noexcept
    {
        if (lhs.size != rhs.size)
        {
            return false;
        }

        if (lhs.size == 0u)
        {
            return true;
        }

        if (!lhs.data || !rhs.data)
        {
            return false;
        }

        return ::memcmp(lhs.data, rhs.data, lhs.size) == 0;
    }

    static dng_status_v1 ValidateModuleInterfaceV1(const dng_module_interface_v1* entry, const dng_host_api_v1* host) noexcept
    {
        if (!entry)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        if (entry->interface_name.size == 0u || entry->interface_name.data == nullptr)
        {
            LogIssue(host, "Module interface name invalid");
            return DNG_STATUS_INVALID_ARG;
        }

        if (!entry->api)
        {
            LogIssue(host, "Module interface api pointer is null");
            return DNG_STATUS_INVALID_ARG;
        }

        if (entry->api->struct_size < sizeof(dng_abi_header_v1))
        {
            LogIssue(host, "Module interface struct_size invalid");
            return DNG_STATUS_INVALID_ARG;
        }

        if (entry->api->abi_version != entry->interface_version)
        {
            LogIssue(host, "Module interface version mismatch");
            return DNG_STATUS_UNSUPPORTED;
        }

        return DNG_STATUS_OK;
    }

    static dng_status_v1 ValidateModuleApiV2(const dng_module_api_v2* api, const dng_host_api_v1* host) noexcept
    {
        if (!api)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->header.struct_size != sizeof(dng_module_api_v2))
        {
            LogIssue(host, "ModuleApi struct_size mismatch");
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->header.abi_version != DNG_MODULE_API_VERSION_V2)
        {
            LogIssue(host, "ModuleApi abi_version mismatch");
            return DNG_STATUS_UNSUPPORTED;
        }

        if (ValidateStrView(api->module_name, "ModuleApi module_name invalid", host) != DNG_STATUS_OK)
        {
            return DNG_STATUS_INVALID_ARG;
        }

        if (api->shutdown && !api->module_ctx)
        {
            LogIssue(host, "ModuleApi module_ctx is null");
            return DNG_STATUS_INVALID_ARG;
        }

        if ((api->interface_count != 0u) && !api->interfaces)
        {
            LogIssue(host, "ModuleApi interface catalogue missing");
            return DNG_STATUS_INVALID_ARG;
        }

        for (dng_u32 i = 0u; i < api->interface_count; ++i)
        {
            const dng_status_v1 interface_ok = ValidateModuleInterfaceV1(&api->interfaces[i], host);
            if (interface_ok != DNG_STATUS_OK)
            {
                return interface_ok;
            }

            for (dng_u32 j = i + 1u; j < api->interface_count; ++j)
            {
                if (api->interfaces[i].interface_version == api->interfaces[j].interface_version &&
                    StrViewEquals(api->interfaces[i].interface_name, api->interfaces[j].interface_name))
                {
                    LogIssue(host, "ModuleApi duplicate interface export");
                    return DNG_STATUS_INVALID_ARG;
                }
            }
        }

        return DNG_STATUS_OK;
    }

#if DNG_PLATFORM_WINDOWS
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

dng_status_v1 ModuleLoader::Load(const char* path, const dng_host_api_v1* host, dng_module_api_v2* outApi) noexcept
{
    if (!path || !host || !outApi)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    ::memset(outApi, 0, sizeof(*outApi));

    const dng_status_v1 host_ok = ValidateHostApiV1(host);
    if (host_ok != DNG_STATUS_OK)
    {
        return host_ok;
    }

    if (m_handle)
    {
        Unload();
    }

#if DNG_PLATFORM_WINDOWS
    HMODULE lib = ::LoadLibraryA(path);
    if (!lib)
    {
        LogWin32Error(host, 1u, "LoadLibraryA failed");
        return DNG_STATUS_FAIL;
    }

    FARPROC proc = ::GetProcAddress(lib, DNG_MODULE_GET_API_V2_NAME);
    if (!proc)
    {
        // Optional x86 fallback: some toolchains decorate __cdecl exports with a leading underscore.
        proc = ::GetProcAddress(lib, "_" DNG_MODULE_GET_API_V2_NAME);
    }

    if (!proc)
    {
        Log(host, 1u, "dngModuleGetApi_v2 not found");
        ::FreeLibrary(lib);
        return DNG_STATUS_UNSUPPORTED;
    }

    auto entry = reinterpret_cast<dng_status_v1 (DNG_ABI_CALL *)(const dng_host_api_v1*, dng_module_api_v2*)>(proc);
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
    void* sym = ::dlsym(lib, DNG_MODULE_GET_API_V2_NAME);
    const char* sym_err = ::dlerror();
    if (sym_err != nullptr)
    {
        Log(host, 1u, sym_err);
        ::dlclose(lib);
        return DNG_STATUS_UNSUPPORTED;
    }
    if (!sym)
    {
        Log(host, 1u, "dlsym returned null for dngModuleGetApi_v2");
        ::dlclose(lib);
        return DNG_STATUS_UNSUPPORTED;
    }

    auto entry = reinterpret_cast<dng_status_v1 (DNG_ABI_CALL *)(const dng_host_api_v1*, dng_module_api_v2*)>(sym);
#endif

    ModuleApiScratchBufferV2* scratch = static_cast<ModuleApiScratchBufferV2*>(
        host->alloc(host->user, sizeof(ModuleApiScratchBufferV2), alignof(ModuleApiScratchBufferV2)));
    if (!scratch)
    {
#if DNG_PLATFORM_WINDOWS
        ::FreeLibrary(lib);
#else
        ::dlclose(lib);
#endif
        return DNG_STATUS_OUT_OF_MEMORY;
    }

    ::memset(scratch, 0, sizeof(*scratch));
    for (dng_u32 i = 0u; i < kModuleApiScratchCanaryCount; ++i)
    {
        scratch->tail_canary[i] = kModuleApiScratchCanary;
    }
    scratch->api.header.struct_size = sizeof(scratch->api);
    scratch->api.header.abi_version = DNG_MODULE_API_VERSION_V2;

    const dng_status_v1 status = entry(host, &scratch->api);

    bool scratch_intact = true;
    for (dng_u32 i = 0u; i < kModuleApiScratchCanaryCount; ++i)
    {
        if (scratch->tail_canary[i] != kModuleApiScratchCanary)
        {
            scratch_intact = false;
            break;
        }
    }

    if (!scratch_intact)
    {
        Log(host, 1u, "Module entrypoint overwrote the ABI scratch buffer");
#if DNG_PLATFORM_WINDOWS
        ::FreeLibrary(lib);
#else
        ::dlclose(lib);
#endif
        host->free(host->user, scratch, sizeof(ModuleApiScratchBufferV2), alignof(ModuleApiScratchBufferV2));
        return DNG_STATUS_UNSUPPORTED;
    }

    if (status != DNG_STATUS_OK)
    {
#if DNG_PLATFORM_WINDOWS
        ::FreeLibrary(lib);
#else
        ::dlclose(lib);
#endif
        host->free(host->user, scratch, sizeof(ModuleApiScratchBufferV2), alignof(ModuleApiScratchBufferV2));
        return status;
    }

    const dng_status_v1 api_ok = ValidateModuleApiV2(&scratch->api, host);
    if (api_ok != DNG_STATUS_OK)
    {
        Log(host, 1u, "Module returned an invalid API table");
#if DNG_PLATFORM_WINDOWS
        ::FreeLibrary(lib);
#else
        ::dlclose(lib);
#endif
        host->free(host->user, scratch, sizeof(ModuleApiScratchBufferV2), alignof(ModuleApiScratchBufferV2));
        return api_ok;
    }

    *outApi = scratch->api;
    host->free(host->user, scratch, sizeof(ModuleApiScratchBufferV2), alignof(ModuleApiScratchBufferV2));
    m_handle = lib;
    return DNG_STATUS_OK;
}

void ModuleLoader::Unload() noexcept
{
    if (!m_handle)
    {
        return;
    }

#if DNG_PLATFORM_WINDOWS
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
