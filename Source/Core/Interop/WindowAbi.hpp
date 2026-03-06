// ============================================================================
// D-Engine - Core/Interop/WindowAbi.hpp
// ----------------------------------------------------------------------------
// Purpose : Thin C++ helpers around dng_window_api_v1 (no ownership changes).
// Contract: Inline wrappers; no allocations; forward status codes; caller must
//           ensure api pointers are valid; no exceptions/RTTI.
// Notes   : Determinism and thread-safety follow the underlying module impl.
//           This header also owns typed lookup/validation for window tables
//           exported through the generic module ABI catalogue.
// ============================================================================
#ifndef DNG_INTEROP_WINDOW_ABI_HPP
#define DNG_INTEROP_WINDOW_ABI_HPP

#include "Core/Abi/DngWindowApi.h"
#include "Core/Interop/ModuleAbi.hpp"

namespace dng
{

[[nodiscard]] inline dng_status_v1 ValidateWindowApiV1(const dng_window_api_v1* api) noexcept
{
    if (!api)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    if (api->header.struct_size != sizeof(dng_window_api_v1))
    {
        return DNG_STATUS_INVALID_ARG;
    }

    if (api->header.abi_version != DNG_ABI_VERSION_V1)
    {
        return DNG_STATUS_UNSUPPORTED;
    }

    if (!api->ctx)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    if (!api->create || !api->destroy || !api->poll || !api->get_size || !api->set_title)
    {
        return DNG_STATUS_INVALID_ARG;
    }

    return DNG_STATUS_OK;
}

[[nodiscard]] inline const dng_window_api_v1* GetWindowApiV1(const dng_module_api_v2& moduleApi) noexcept
{
    const dng_module_interface_v1* entry =
        FindModuleInterface(moduleApi, ModuleAbiLiteral(DNG_MODULE_INTERFACE_NAME_WINDOW), DNG_ABI_VERSION_V1);
    if (!entry)
    {
        return nullptr;
    }

    const dng_window_api_v1* api = reinterpret_cast<const dng_window_api_v1*>(entry->api);
    return ValidateWindowApiV1(api) == DNG_STATUS_OK ? api : nullptr;
}

inline dng_status_v1 WindowCreate(const dng_window_api_v1& api, const dng_window_desc_v1* desc, dng_window_handle_v1* out_handle) noexcept
{
    return (api.create && api.ctx) ? api.create(api.ctx, desc, out_handle) : DNG_STATUS_INVALID_ARG;
}

inline dng_status_v1 WindowDestroy(const dng_window_api_v1& api, dng_window_handle_v1 handle) noexcept
{
    return (api.destroy && api.ctx) ? api.destroy(api.ctx, handle) : DNG_STATUS_INVALID_ARG;
}

inline dng_status_v1 WindowPoll(const dng_window_api_v1& api) noexcept
{
    return (api.poll && api.ctx) ? api.poll(api.ctx) : DNG_STATUS_INVALID_ARG;
}

inline dng_status_v1 WindowGetSize(const dng_window_api_v1& api, dng_window_handle_v1 handle, dng_window_size_v1* out_size) noexcept
{
    return (api.get_size && api.ctx) ? api.get_size(api.ctx, handle, out_size) : DNG_STATUS_INVALID_ARG;
}

inline dng_status_v1 WindowSetTitle(const dng_window_api_v1& api, dng_window_handle_v1 handle, dng_str_view_v1 title) noexcept
{
    return (api.set_title && api.ctx) ? api.set_title(api.ctx, handle, title) : DNG_STATUS_INVALID_ARG;
}

} // namespace dng

#endif // DNG_INTEROP_WINDOW_ABI_HPP
