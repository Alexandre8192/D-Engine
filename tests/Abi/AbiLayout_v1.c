// ABI layout enforcement for v1 (C11)
#include <assert.h>
#include <stddef.h>
#include <stdalign.h>

#include "Core/Abi/DngAbi.h"
#include "Core/Abi/DngHostApi.h"
#include "Core/Abi/DngModuleApi.h"
#include "Core/Abi/DngWindowApi.h"

#define DNG_ALIGN_UP(value, align) (((value) + ((align) - 1u)) / (align) * (align))

// Core POD sizes and alignments
static_assert(sizeof(dng_abi_header_v1) == 8u, "dng_abi_header_v1 size");
static_assert(_Alignof(dng_abi_header_v1) == _Alignof(dng_u32), "dng_abi_header_v1 align");

static_assert(sizeof(dng_status_v1) == sizeof(dng_u32), "dng_status_v1 size");
static_assert(_Alignof(dng_status_v1) == _Alignof(dng_u32), "dng_status_v1 align");

static_assert(sizeof(dng_bool_v1) == sizeof(dng_u8), "dng_bool_v1 size");
static_assert(_Alignof(dng_bool_v1) == _Alignof(dng_u8), "dng_bool_v1 align");

// dng_str_view_v1 layout
static_assert(offsetof(dng_str_view_v1, data) == 0u, "dng_str_view_v1.data offset");
static_assert(offsetof(dng_str_view_v1, size) == sizeof(const char*), "dng_str_view_v1.size offset");
static_assert(_Alignof(dng_str_view_v1) == _Alignof(const char*), "dng_str_view_v1 align");
static_assert(
    sizeof(dng_str_view_v1) == DNG_ALIGN_UP(sizeof(const char*) + sizeof(dng_u32), _Alignof(const char*)),
    "dng_str_view_v1 size");

// Handles
static_assert(sizeof(dng_window_handle_v1) == sizeof(dng_u64), "dng_window_handle_v1 size");
static_assert(_Alignof(dng_window_handle_v1) == _Alignof(dng_u64), "dng_window_handle_v1 align");

// ABI table headers are first
static_assert(offsetof(dng_window_api_v1, header) == 0u, "dng_window_api_v1.header offset");
static_assert(sizeof(((dng_window_api_v1*)0)->header) == sizeof(dng_abi_header_v1), "dng_window_api_v1.header size");
static_assert(_Alignof(dng_window_api_v1) == _Alignof(void*), "dng_window_api_v1 align");

static_assert(offsetof(dng_module_api_v1, header) == 0u, "dng_module_api_v1.header offset");
static_assert(sizeof(((dng_module_api_v1*)0)->header) == sizeof(dng_abi_header_v1), "dng_module_api_v1.header size");
static_assert(_Alignof(dng_module_api_v1) == _Alignof(void*), "dng_module_api_v1 align");

// Ensure header struct_size fields are at offset 0 (caller must set to sizeof(struct))
static_assert(offsetof(dng_abi_header_v1, struct_size) == 0u, "dng_abi_header_v1.struct_size offset");
static_assert(offsetof(dng_window_api_v1, header.struct_size) == 0u, "dng_window_api_v1.struct_size offset");
static_assert(offsetof(dng_module_api_v1, header.struct_size) == 0u, "dng_module_api_v1.struct_size offset");

// Reserved/expansion fields expectations
static_assert(offsetof(dng_window_desc_v1, flags) == offsetof(dng_window_desc_v1, flags), "dng_window_desc_v1.flags present");

int AbiLayout_v1_CompileSentinel(void)
{
    // Provide a tiny runtime check that struct_size expectations match sizeof.
    const dng_window_api_v1 window_api = { { (dng_u32)sizeof(dng_window_api_v1), DNG_ABI_VERSION_V1 }, NULL, NULL, NULL, NULL, NULL, NULL };
    const dng_module_api_v1 module_api = { { (dng_u32)sizeof(dng_module_api_v1), DNG_ABI_VERSION_V1 }, { NULL, 0u }, 0u, 0u, 0u, window_api, NULL };

    int ok = 0;
    ok |= (window_api.header.struct_size == sizeof(dng_window_api_v1)) ? 0 : 1;
    ok |= (module_api.header.struct_size == sizeof(dng_module_api_v1)) ? 0 : 1;
    return ok;
}
