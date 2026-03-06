// Compile-only C99 check for ABI headers.
#include "Core/Abi/DngAbi.h"
#include "Core/Abi/DngHostApi.h"
#include "Core/Abi/DngModuleApi.h"
#include "Core/Abi/DngWindowApi.h"

int main(void)
{
    dng_abi_header_v1 header;
    dng_module_api_v2 module_api;
    header.struct_size = (dng_u32)sizeof(dng_abi_header_v1);
    header.abi_version = DNG_ABI_VERSION_V1;
    module_api.header.struct_size = (dng_u32)sizeof(dng_module_api_v2);
    module_api.header.abi_version = DNG_MODULE_API_VERSION_V2;
    (void)header;
    (void)module_api;
    return 0;
}
