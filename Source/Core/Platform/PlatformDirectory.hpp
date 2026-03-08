// ============================================================================
// D-Engine - Source/Core/Platform/PlatformDirectory.hpp
// ----------------------------------------------------------------------------
// Purpose : Route simple directory creation helpers through the shared
//           platform layer so callers do not branch on OS headers directly.
// Contract: Header-only, no heap allocations, no exceptions/RTTI.
// Notes   : Intended for small tooling/runtime helpers, not as a full
//           filesystem abstraction.
// ============================================================================

#pragma once

#include "PlatformDefines.hpp"

#include <cstddef>

#if DNG_PLATFORM_WINDOWS
    #include "WindowsApi.hpp"
#else
    #include <errno.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

namespace dng::platform
{
    [[nodiscard]] constexpr char NativePathSeparator() noexcept
    {
#if DNG_PLATFORM_WINDOWS
        return '\\';
#else
        return '/';
#endif
    }

    [[nodiscard]] inline bool CreateDirectoryLeaf(const char* path) noexcept
    {
        if (path == nullptr || path[0] == '\0')
        {
            return false;
        }

#if DNG_PLATFORM_WINDOWS
        if (::CreateDirectoryA(path, nullptr) != FALSE)
        {
            return true;
        }
        return ::GetLastError() == ERROR_ALREADY_EXISTS;
#else
        if (::mkdir(path, 0777) == 0)
        {
            return true;
        }
        return errno == EEXIST;
#endif
    }

    [[nodiscard]] inline bool EnsureDirectoryTree(const char* path) noexcept
    {
        if (path == nullptr || path[0] == '\0')
        {
            return false;
        }

        constexpr std::size_t kMaxPathBytes = 1024;
        char buffer[kMaxPathBytes + 1]{};
        std::size_t length = 0;
        const char separator = NativePathSeparator();

        for (const char* cursor = path; ; ++cursor)
        {
            const char c = *cursor;
            const bool atEnd = (c == '\0');
            const bool isSeparator = !atEnd && (c == '/' || c == '\\');

            if (!atEnd)
            {
                if (length >= kMaxPathBytes)
                {
                    return false;
                }
                buffer[length++] = isSeparator ? separator : c;
            }

            if (isSeparator || atEnd)
            {
                std::size_t prefixLength = length;
                while (prefixLength > 0 && buffer[prefixLength - 1] == separator)
                {
                    --prefixLength;
                }

                if (prefixLength > 0)
                {
#if DNG_PLATFORM_WINDOWS
                    if (!(prefixLength == 2 && buffer[1] == ':'))
#endif
                    {
                        const char saved = buffer[prefixLength];
                        buffer[prefixLength] = '\0';
                        if (!CreateDirectoryLeaf(buffer))
                        {
                            return false;
                        }
                        buffer[prefixLength] = saved;
                    }
                }

                if (atEnd)
                {
                    break;
                }
            }
        }

        return true;
    }
} // namespace dng::platform
