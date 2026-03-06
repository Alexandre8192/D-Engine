// ============================================================================
// D-Engine - Core/Interop/ModuleAbi.hpp
// ----------------------------------------------------------------------------
// Purpose : Thin C++ helpers around the generic module ABI catalogue.
// Contract: Inline wrappers only; no allocations; no ownership changes; caller
//           remains responsible for module lifetime and shutdown ordering.
// Notes   : Provides lookup helpers for typed subsystem tables exported through
//           dng_module_api_v2.
// ============================================================================
#ifndef DNG_INTEROP_MODULE_ABI_HPP
#define DNG_INTEROP_MODULE_ABI_HPP

#include "Core/Abi/DngModuleApi.h"
#include "Core/Abi/DngWindowApi.h"

#include <cstddef>
#include <cstring>

namespace dng
{

template <std::size_t N>
[[nodiscard]] constexpr dng_str_view_v1 ModuleAbiLiteral(const char (&text)[N]) noexcept
{
    static_assert(N > 0u, "ABI literal must include a terminating null");

    dng_str_view_v1 view{};
    view.data = text;
    view.size = static_cast<dng_u32>(N - 1u);
    return view;
}

[[nodiscard]] inline bool ModuleAbiEquals(dng_str_view_v1 lhs, dng_str_view_v1 rhs) noexcept
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

    return std::memcmp(lhs.data, rhs.data, lhs.size) == 0;
}

[[nodiscard]] inline const dng_module_interface_v1* FindModuleInterface(const dng_module_api_v2& moduleApi,
    dng_str_view_v1 interfaceName,
    dng_u32 interfaceVersion) noexcept
{
    if (!moduleApi.interfaces || moduleApi.interface_count == 0u)
    {
        return nullptr;
    }

    for (dng_u32 index = 0u; index < moduleApi.interface_count; ++index)
    {
        const dng_module_interface_v1& entry = moduleApi.interfaces[index];
        if (entry.interface_version != interfaceVersion)
        {
            continue;
        }

        if (ModuleAbiEquals(entry.interface_name, interfaceName))
        {
            return &entry;
        }
    }

    return nullptr;
}

[[nodiscard]] inline const dng_window_api_v1* GetWindowApiV1(const dng_module_api_v2& moduleApi) noexcept
{
    const dng_module_interface_v1* entry =
        FindModuleInterface(moduleApi, ModuleAbiLiteral(DNG_MODULE_INTERFACE_NAME_WINDOW), DNG_ABI_VERSION_V1);
    return entry ? reinterpret_cast<const dng_window_api_v1*>(entry->api) : nullptr;
}

} // namespace dng

#endif // DNG_INTEROP_MODULE_ABI_HPP
