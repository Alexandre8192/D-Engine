// ============================================================================
// D-Engine - Modules/NullWindowModule/NullWindowModule.cpp
// ----------------------------------------------------------------------------
// Purpose : Minimal loadable module implementing the Window ABI v1 (null impl).
// Contract: C ABI; POD-only ABI structs; no exceptions/RTTI; single-window only;
//           host allocator used for title copies; caller owns module lifetime.
// Notes   : set_title allocates via host->alloc and frees previous via host->free;
//           not intended for hot paths. Determinism follows host pump cadence.
// ============================================================================
#define DNG_ABI_EXPORTS
#include "Core/Abi/DngModuleApi.h"

#include <string.h>

typedef struct NullWindowCtx {
    const dng_host_api_v1* host; // Non-owning pointer to host services.
    dng_window_handle_v1   handle;
    dng_window_size_v1     size;
    char*                  title;
    dng_u32                title_size;
} NullWindowCtx;

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
        ctx->host->free(ctx->host->user, ctx->title, ctx->title_size, 1u);
        ctx->title = NULL;
        ctx->title_size = 0u;
    }

    if (!title.data || title.size == 0u)
    {
        return DNG_STATUS_OK;
    }

    void* mem = ctx->host->alloc(ctx->host->user, title.size, 1u);
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
        ctx->host->free(ctx->host->user, ctx->title, ctx->title_size, 1u);
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

static void NullWindow_FillModuleApi(NullWindowCtx* ctx, dng_module_api_v1* api)
{
    api->header.struct_size = (dng_u32)sizeof(dng_module_api_v1);
    api->header.abi_version = DNG_ABI_VERSION_V1;

    api->module_name.data = "NullWindowModule";
    api->module_name.size = NullWindow_StrLen(api->module_name.data);
    api->module_version_major = 1u;
    api->module_version_minor = 0u;
    api->module_version_patch = 0u;

    NullWindow_InitWindowApi(ctx, &api->window);
}

DNG_ABI_API dng_status_v1 DNG_ABI_CALL dngModuleGetApi_v1(const dng_host_api_v1* host, dng_module_api_v1* out_api)
{
    if (!host || !out_api || !host->alloc || !host->free)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    static NullWindowCtx ctx;
    ctx.host = host;
    ctx.handle = 0u;
    ctx.size.width = 0u;
    ctx.size.height = 0u;
    ctx.title = NULL;
    ctx.title_size = 0u;

    NullWindow_FillModuleApi(&ctx, out_api);
    return DNG_STATUS_OK;
}
