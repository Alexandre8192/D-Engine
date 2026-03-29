// Basic smoke test for ABI module loading and Window API calls.
#include "Core/Interop/ModuleAbi.hpp"
#include "Core/Interop/ModuleLoader.hpp"
#include "Core/Interop/WindowAbi.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <malloc.h>
    static const char* kModulePath = "NullWindowModule.dll";
#elif defined(__APPLE__)
    static const char* kModulePath = "libNullWindowModule.dylib";
#else
    static const char* kModulePath = "libNullWindowModule.so";
#endif

// Simple host services for test only (uses CRT alloc).
static void* DNG_ABI_CALL TestAlloc(void* user, dng_u64 size, dng_u64 align)
{
    (void)user;
    if (align < sizeof(void*))
    {
        align = sizeof(void*);
    }
#if defined(_WIN32) || defined(_WIN64)
    return _aligned_malloc((size_t)size, (size_t)align);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, (size_t)align, (size_t)size) != 0)
    {
        return NULL;
    }
    return ptr;
#endif
}

static void DNG_ABI_CALL TestFree(void* user, void* ptr, dng_u64 size, dng_u64 align)
{
    (void)user;
    (void)size;
    (void)align;
#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

static void DNG_ABI_CALL TestLog(void* user, dng_u32 level, dng_str_view_v1 msg)
{
    (void)user;
    (void)level;
    fwrite(msg.data, 1, msg.size, stdout);
    fputc('\n', stdout);
}

int main()
{
    dng_host_api_v1 host = {};
    host.header.struct_size = (dng_u32)sizeof(host);
    host.header.abi_version = DNG_ABI_VERSION_V1;
    host.user = NULL;
    host.log = &TestLog;
    host.alloc = &TestAlloc;
    host.free = &TestFree;

    dng::ModuleLoader loader;
    dng_module_api_v2 module_api = {};
    dng_status_v1 status = loader.Load(kModulePath, &host, &module_api);
    if (status != DNG_STATUS_OK)
    {
        printf("Load failed: %u\n", (unsigned)status);
        return 1;
    }

    const dng_window_api_v1* window_api = dng::GetWindowApiV1(module_api);
    if (!window_api)
    {
        printf("Module did not expose dng.window v1\n");
        return 2;
    }

    if (dng::FindModuleInterface(module_api, dng::ModuleAbiLiteral("dng.audio"), DNG_ABI_VERSION_V1) != nullptr)
    {
        printf("Unexpected audio interface export\n");
        return 3;
    }

    dng_window_desc_v1 desc = {};
    desc.width = 640u;
    desc.height = 480u;
    desc.title.data = "TestWindow";
    desc.title.size = (dng_u32)strlen(desc.title.data);
    desc.flags = 0u;

    dng_window_handle_v1 handle = 0u;

    // Negative: v1 reserves flags; non-zero must be rejected.
    dng_window_desc_v1 bad_desc = desc;
    bad_desc.flags = 1u;
    dng_window_handle_v1 bad_handle = 0u;
    status = dng::WindowCreate(*window_api, &bad_desc, &bad_handle);
    if (status != DNG_STATUS_INVALID_ARG || bad_handle != 0u)
    {
        printf("Create with invalid flags did not fail as expected: %u\n", (unsigned)status);
        return 4;
    }

    status = dng::WindowCreate(*window_api, &desc, &handle);
    if (status != DNG_STATUS_OK || handle == 0u)
    {
        printf("Create failed: %u\n", (unsigned)status);
        return 5;
    }

    dng_window_size_v1 size = {};
    status = dng::WindowGetSize(*window_api, handle, &size);
    if (status != DNG_STATUS_OK || size.width != desc.width || size.height != desc.height)
    {
        printf("GetSize failed: %u\n", (unsigned)status);
        return 6;
    }

    dng_str_view_v1 new_title = { "Updated", 7u };
    status = dng::WindowSetTitle(*window_api, handle, new_title);
    if (status != DNG_STATUS_OK)
    {
        printf("SetTitle failed: %u\n", (unsigned)status);
        return 7;
    }

    // Negative: non-empty title requires non-null pointer.
    dng_str_view_v1 bad_title = { NULL, 1u };
    status = dng::WindowSetTitle(*window_api, handle, bad_title);
    if (status != DNG_STATUS_INVALID_ARG)
    {
        printf("SetTitle with invalid view did not fail as expected: %u\n", (unsigned)status);
        return 8;
    }

    status = dng::WindowPoll(*window_api);
    if (status != DNG_STATUS_OK)
    {
        printf("Poll failed: %u\n", (unsigned)status);
        return 9;
    }

    status = dng::WindowDestroy(*window_api, handle);
    if (status != DNG_STATUS_OK)
    {
        printf("Destroy failed: %u\n", (unsigned)status);
        return 10;
    }

    // Negative: window handle is invalid once destroyed.
    dng_window_size_v1 size_after_destroy = {};
    status = dng::WindowGetSize(*window_api, handle, &size_after_destroy);
    if (status != DNG_STATUS_INVALID_ARG)
    {
        printf("GetSize after destroy did not fail as expected: %u\n", (unsigned)status);
        return 11;
    }

    status = dng::WindowDestroy(*window_api, handle);
    if (status != DNG_STATUS_INVALID_ARG)
    {
        printf("Destroy twice did not fail as expected: %u\n", (unsigned)status);
        return 12;
    }

    if (module_api.shutdown)
    {
        status = module_api.shutdown(module_api.module_ctx, &host);
        if (status != DNG_STATUS_OK)
        {
            printf("Shutdown failed: %u\n", (unsigned)status);
            return 13;
        }
        // Shutdown is single-use for dynamically allocated contexts.
        module_api.module_ctx = NULL;
        module_api.interfaces = NULL;
        module_api.interface_count = 0u;
        module_api.shutdown = NULL;
    }

    loader.Unload();
    return 0;
}
