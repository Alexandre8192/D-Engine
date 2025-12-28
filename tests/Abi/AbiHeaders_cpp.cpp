// Compile-only C++ check for ABI headers.
#include "Core/Abi/DngAbi.h"
#include "Core/Abi/DngHostApi.h"
#include "Core/Abi/DngModuleApi.h"
#include "Core/Abi/DngWindowApi.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout<dng_abi_header_v1>::value, "dng_abi_header_v1 must be standard layout");
static_assert(offsetof(dng_abi_header_v1, struct_size) == 0, "struct_size first");
static_assert(offsetof(dng_abi_header_v1, abi_version) == sizeof(dng_u32), "abi_version second");
static_assert(sizeof(dng_abi_header_v1) == sizeof(dng_u32) * 2u, "header size");

static_assert(std::is_pod<dng_str_view_v1>::value, "string view must be POD");
static_assert(std::is_standard_layout<dng_window_api_v1>::value, "window api layout");

int main()
{
    dng_module_api_v1 api = {};
    api.header.struct_size = sizeof(api);
    api.header.abi_version = DNG_ABI_VERSION_V1;
    return 0;
}
