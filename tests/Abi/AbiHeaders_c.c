// Compile-only C99 check for ABI headers.
#include "Core/Abi/DngAbi.h"
#include "Core/Abi/DngHostApi.h"
#include "Core/Abi/DngModuleApi.h"
#include "Core/Abi/DngWindowApi.h"

int main(void)
{
    dng_abi_header_v1 header;
    header.struct_size = (dng_u32)sizeof(dng_abi_header_v1);
    header.abi_version = DNG_ABI_VERSION_V1;
    (void)header;
    return 0;
}
