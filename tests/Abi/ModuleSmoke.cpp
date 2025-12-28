// Basic smoke test for ABI module loading and Window API calls.
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
    dng_module_api_v1 module_api = {};
    dng_status_v1 status = loader.Load(kModulePath, &host, &module_api);
    if (status != DNG_STATUS_OK)
    {
        printf("Load failed: %u\n", (unsigned)status);
        return 1;
    }

    dng_window_desc_v1 desc = {};
    desc.width = 640u;
    desc.height = 480u;
    desc.title.data = "TestWindow";
    desc.title.size = (dng_u32)strlen(desc.title.data);
    desc.flags = 0u;

    dng_window_handle_v1 handle = 0u;
    status = dng::WindowCreate(module_api.window, &desc, &handle);
    if (status != DNG_STATUS_OK || handle == 0u)
    {
        printf("Create failed: %u\n", (unsigned)status);
        return 2;
    }

    dng_window_size_v1 size = {};
    status = dng::WindowGetSize(module_api.window, handle, &size);
    if (status != DNG_STATUS_OK || size.width != desc.width || size.height != desc.height)
    {
        printf("GetSize failed: %u\n", (unsigned)status);
        return 3;
    }

    dng_str_view_v1 new_title = { "Updated", 7u };
    status = dng::WindowSetTitle(module_api.window, handle, new_title);
    if (status != DNG_STATUS_OK)
    {
        printf("SetTitle failed: %u\n", (unsigned)status);
        return 4;
    }

    status = dng::WindowPoll(module_api.window);
    if (status != DNG_STATUS_OK)
    {
        printf("Poll failed: %u\n", (unsigned)status);
        return 5;
    }

    status = dng::WindowDestroy(module_api.window, handle);
    if (status != DNG_STATUS_OK)
    {
        printf("Destroy failed: %u\n", (unsigned)status);
        return 6;
    }

    if (module_api.shutdown)
    {
        status = module_api.shutdown(module_api.window.ctx, &host);
        if (status != DNG_STATUS_OK)
        {
            printf("Shutdown failed: %u\n", (unsigned)status);
            return 7;
        }
    }

    loader.Unload();
    return 0;
}
