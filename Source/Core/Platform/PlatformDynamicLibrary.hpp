// ============================================================================
// D-Engine - Source/Core/Platform/PlatformDynamicLibrary.hpp
// ----------------------------------------------------------------------------
// Purpose : Centralize shared-library loading through the platform layer.
// Contract: Header-only, no ownership beyond explicit load/unload calls.
// Notes   : Keep this small and low-level; typed ABI validation belongs in
//           higher interop layers.
// ============================================================================

#pragma once

#include "PlatformDefines.hpp"

#if DNG_PLATFORM_WINDOWS
    #include "WindowsApi.hpp"
#else
    #include <dlfcn.h>
#endif

namespace dng::platform
{
    using SharedLibraryHandle = void*;

    [[nodiscard]] inline SharedLibraryHandle LoadSharedLibrary(const char* path) noexcept
    {
#if DNG_PLATFORM_WINDOWS
        return reinterpret_cast<SharedLibraryHandle>(::LoadLibraryA(path));
#else
        return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    }

    inline void UnloadSharedLibrary(SharedLibraryHandle handle) noexcept
    {
        if (handle == nullptr)
        {
            return;
        }

#if DNG_PLATFORM_WINDOWS
        (void)::FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
        (void)::dlclose(handle);
#endif
    }

    [[nodiscard]] inline void* LoadSharedLibrarySymbol(SharedLibraryHandle handle, const char* name) noexcept
    {
        if (handle == nullptr || name == nullptr)
        {
            return nullptr;
        }

#if DNG_PLATFORM_WINDOWS
        return reinterpret_cast<void*>(
            ::GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
        return ::dlsym(handle, name);
#endif
    }

    inline void ClearSharedLibraryErrorState() noexcept
    {
#if !DNG_PLATFORM_WINDOWS
        (void)::dlerror();
#endif
    }

    [[nodiscard]] inline const char* GetSharedLibraryLastErrorMessage() noexcept
    {
#if DNG_PLATFORM_WINDOWS
        return nullptr;
#else
        return ::dlerror();
#endif
    }

    [[nodiscard]] inline unsigned long GetLastOsErrorCode() noexcept
    {
#if DNG_PLATFORM_WINDOWS
        return static_cast<unsigned long>(::GetLastError());
#else
        return 0ul;
#endif
    }
} // namespace dng::platform
