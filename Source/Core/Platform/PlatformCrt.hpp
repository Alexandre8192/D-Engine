// ============================================================================
// D-Engine - Source/Core/Platform/PlatformCrt.hpp
// ----------------------------------------------------------------------------
// Purpose : Route small CRT/compiler-specific calls through a shared platform
//           layer so higher code does not branch on toolchain macros directly.
// Contract: Header-only, no exceptions/RTTI, no ownership beyond returned CRT
//           handles/pointers.
// Notes   : Intentionally small; extend only for common cross-cutting helpers.
// ============================================================================

#pragma once

#include "PlatformCompiler.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#if DNG_PLATFORM_WINDOWS
    #include <malloc.h>
#endif

namespace dng::platform
{
    [[nodiscard]] inline const char* GetEnvNoWarn(const char* name) noexcept
    {
#if DNG_COMPILER_MSVC
    #pragma warning(push)
    #pragma warning(disable:4996)
#endif
        return std::getenv(name);
#if DNG_COMPILER_MSVC
    #pragma warning(pop)
#endif
    }

    [[nodiscard]] inline bool OpenFile(const char* path, const char* mode, std::FILE*& outFile) noexcept
    {
        outFile = nullptr;
        if (path == nullptr || mode == nullptr)
        {
            return false;
        }

#if DNG_COMPILER_MSVC
    #pragma warning(push)
    #pragma warning(disable:4996)
#endif
        outFile = std::fopen(path, mode);
#if DNG_COMPILER_MSVC
    #pragma warning(pop)
#endif
        return outFile != nullptr;
    }

    [[nodiscard]] inline bool OpenFileReadBinary(const char* path, std::FILE*& outFile) noexcept
    {
        return OpenFile(path, "rb", outFile);
    }

    [[nodiscard]] inline bool OpenFileWriteBinary(const char* path, std::FILE*& outFile) noexcept
    {
        return OpenFile(path, "wb", outFile);
    }

    [[nodiscard]] inline void* AllocAligned(std::size_t size, std::size_t align) noexcept
    {
        if (align < sizeof(void*))
        {
            align = sizeof(void*);
        }

#if DNG_PLATFORM_WINDOWS
        return _aligned_malloc(size, align);
#else
        (void)size;
        (void)align;
        return nullptr;
#endif
    }

    inline void FreeAligned(void* ptr) noexcept
    {
#if DNG_PLATFORM_WINDOWS
        _aligned_free(ptr);
#else
        (void)ptr;
#endif
    }
} // namespace dng::platform
