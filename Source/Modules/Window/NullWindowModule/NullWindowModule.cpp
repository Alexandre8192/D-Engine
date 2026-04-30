// ============================================================================
// D-Engine - Modules/Window/NullWindowModule/NullWindowModule.cpp
// ----------------------------------------------------------------------------
// Purpose : Minimal loadable module implementing the Window ABI v1 (null impl).
// Contract: C ABI; POD-only ABI structs; no exceptions/RTTI; single-window only;
//           context allocated via host->alloc in dngModuleGetApi_v2 and freed
//           in shutdown; host allocator used for title copies; caller must call
//           shutdown exactly once before module unload. The exported module API
//           table references data stored inside the persistent NullWindowCtx, so
//           module_ctx, the interface catalogue, and the window API payload stay
//           valid until shutdown returns.
// Notes   : set_title allocates via host->alloc and frees previous via host->free;
//           not intended for hot paths. Determinism follows host pump cadence.
//           Context is thread-safe per-instance (one context per module load).
// ============================================================================
#define DNG_ABI_EXPORTS
#include "Core/Abi/DngModuleApi.h"
#include "Core/Abi/DngWindowApi.h"

#include <string.h>

typedef struct NullWindowCtx {
    const dng_host_api_v1*  host; // Non-owning pointer to host services.
    dng_window_handle_v1    handle;
    dng_window_size_v1      size;
    char*                   title;
    dng_u32                 title_size;
    dng_window_api_v1       window_api;
    dng_module_interface_v1 interface_entry;
} NullWindowCtx;

// Context and title allocation constants (size and align must match free).
static const dng_u64 kNullWindowCtxSize = sizeof(NullWindowCtx);
static const dng_u64 kNullWindowCtxAlign = alignof(NullWindowCtx);
static const dng_u64 kTitleAlign = 1u; // Byte alignment for title strings.

static dng_u32 NullWindow_StrLen(const char* cstr)
{
    if (!cstr)
    {
        return 0u;
    }
    const size_t len = strlen(cstr);
    return len > 0xFFFFFFFFu ? 0xFFFFFFFFu : (dng_u32)len;
}

static dng_status_v1 NullWindow_SetTitleInternal(NullWindowCtx* ctx, dng_str_view_v1 title)
{
    if (!ctx || !ctx->host || !ctx->host->alloc || !ctx->host->free)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    if (ctx->title)
    {
        ctx->host->free(ctx->host->user, ctx->title, ctx->title_size, kTitleAlign);
        ctx->title = NULL;
        ctx->title_size = 0u;
    }

    if (title.size == 0u)
    {
        return DNG_STATUS_OK;
    }

    if (title.data == NULL)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    void* mem = ctx->host->alloc(ctx->host->user, title.size, kTitleAlign);
    if (!mem)
    {
        return DNG_STATUS_OUT_OF_MEMORY;
    }

    memcpy(mem, title.data, title.size);
    ctx->title = (char*)mem;
    ctx->title_size = title.size;
    return DNG_STATUS_OK;
}

static dng_status_v1 DNG_ABI_CALL NullWindow_Create(void* raw_ctx, const dng_window_desc_v1* desc, dng_window_handle_v1* out_handle)
{
    NullWindowCtx* ctx = (NullWindowCtx*)raw_ctx;
    if (!ctx || !desc || !out_handle)
    {
        return DNG_STATUS_INVALID_ARG;
    }
    if (desc->flags != 0u)
    {
        return DNG_STATUS_INVALID_ARG;
    }
    if (ctx->handle != 0u)
    {
        return DNG_STATUS_FAIL; // Only one window supported.
    }

    ctx->size.width = desc->width;
    ctx->size.height = desc->height;

    const dng_status_v1 title_status = NullWindow_SetTitleInternal(ctx, desc->title);
    if (title_status != DNG_STATUS_OK)
    {
        return title_status;
    }

    ctx->handle = 1u;
    *out_handle = ctx->handle;
    return DNG_STATUS_OK;
}

static dng_status_v1 DNG_ABI_CALL NullWindow_Destroy(void* raw_ctx, dng_window_handle_v1 handle)
{
    NullWindowCtx* ctx = (NullWindowCtx*)raw_ctx;
    if (!ctx || handle == 0u || ctx->handle != handle)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    if (ctx->title)
    {
        ctx->host->free(ctx->host->user, ctx->title, ctx->title_size, kTitleAlign);
    }

    ctx->title = NULL;
    ctx->title_size = 0u;
    ctx->handle = 0u;
    ctx->size.width = 0u;
    ctx->size.height = 0u;
    return DNG_STATUS_OK;
}

static dng_status_v1 DNG_ABI_CALL NullWindow_Poll(void* raw_ctx)
{
    (void)raw_ctx;
    return DNG_STATUS_OK;
}

static dng_status_v1 DNG_ABI_CALL NullWindow_GetSize(void* raw_ctx, dng_window_handle_v1 handle, dng_window_size_v1* out_size)
{
    NullWindowCtx* ctx = (NullWindowCtx*)raw_ctx;
    if (!ctx || !out_size || handle == 0u || ctx->handle != handle)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    *out_size = ctx->size;
    return DNG_STATUS_OK;
}

static dng_status_v1 DNG_ABI_CALL NullWindow_SetTitle(void* raw_ctx, dng_window_handle_v1 handle, dng_str_view_v1 title)
{
    NullWindowCtx* ctx = (NullWindowCtx*)raw_ctx;
    if (!ctx || handle == 0u || ctx->handle != handle)
    {
        return DNG_STATUS_INVALID_ARG;
    }
    return NullWindow_SetTitleInternal(ctx, title);
}

static void NullWindow_InitWindowApi(NullWindowCtx* ctx, dng_window_api_v1* api)
{
    api->header.struct_size = (dng_u32)sizeof(dng_window_api_v1);
    api->header.abi_version = DNG_ABI_VERSION_V1;
    api->ctx = ctx;
    api->create = &NullWindow_Create;
    api->destroy = &NullWindow_Destroy;
    api->poll = &NullWindow_Poll;
    api->get_size = &NullWindow_GetSize;
    api->set_title = &NullWindow_SetTitle;
}

static dng_status_v1 DNG_ABI_CALL NullWindow_Shutdown(void* raw_ctx, const dng_host_api_v1* host)
{
    if (!raw_ctx || !host || !host->free)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    NullWindowCtx* ctx = (NullWindowCtx*)raw_ctx;

    // Free any window title allocation.
    if (ctx->title)
    {
        host->free(host->user, ctx->title, ctx->title_size, kTitleAlign);
        ctx->title = NULL;
        ctx->title_size = 0u;
    }

    // Free the context itself (size and align must match allocation).
    host->free(host->user, raw_ctx, kNullWindowCtxSize, kNullWindowCtxAlign);
    return DNG_STATUS_OK;
}

static void NullWindow_FillModuleApi(NullWindowCtx* ctx, dng_module_api_v2* api)
{
    // Store exported tables inside the persistent module context so every
    // published pointer remains valid until shutdown/module unload.
    api->header.struct_size = (dng_u32)sizeof(dng_module_api_v2);
    api->header.abi_version = DNG_MODULE_API_VERSION_V2;

    api->module_name.data = "NullWindowModule";
    api->module_name.size = NullWindow_StrLen(api->module_name.data);
    api->module_version_major = 1u;
    api->module_version_minor = 0u;
    api->module_version_patch = 0u;

    api->module_ctx = ctx;
    api->interfaces = &ctx->interface_entry;
    api->interface_count = 1u;
    api->shutdown = &NullWindow_Shutdown;

    NullWindow_InitWindowApi(ctx, &ctx->window_api);
    ctx->interface_entry.interface_name.data = DNG_MODULE_INTERFACE_NAME_WINDOW;
    ctx->interface_entry.interface_name.size = NullWindow_StrLen(DNG_MODULE_INTERFACE_NAME_WINDOW);
    ctx->interface_entry.interface_version = DNG_ABI_VERSION_V1;
    ctx->interface_entry.api = (const dng_abi_header_v1*)&ctx->window_api;
}

DNG_ABI_API dng_status_v1 DNG_ABI_CALL dngModuleGetApi_v2(const dng_host_api_v1* host, dng_module_api_v2* out_api)
{
    if (!host || !out_api || !host->alloc || !host->free)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    if (out_api->header.struct_size != 0u && out_api->header.struct_size < sizeof(dng_module_api_v2))
    {
        return DNG_STATUS_UNSUPPORTED;
    }

    if (out_api->header.abi_version != 0u && out_api->header.abi_version != DNG_MODULE_API_VERSION_V2)
    {
        return DNG_STATUS_UNSUPPORTED;
    }

    // Allocate module-owned context via host allocator; exported pointers refer
    // back to this storage and therefore remain stable until shutdown.
    void* mem = host->alloc(host->user, kNullWindowCtxSize, kNullWindowCtxAlign);
    if (!mem)
    {
        return DNG_STATUS_OUT_OF_MEMORY;
    }

    NullWindowCtx* ctx = (NullWindowCtx*)mem;
    memset(ctx, 0, (size_t)kNullWindowCtxSize);
    ctx->host = host;

    NullWindow_FillModuleApi(ctx, out_api);
    return DNG_STATUS_OK;
}
