// ============================================================================
// D-Engine - Source/Core/Contracts/FileSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : File system contract describing backend-agnostic, read-only file
//           queries for existence, size, and simple reads without exposing
//           platform details or performing allocations.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is delegated to the backend owner.
// Notes   : Public API is `PathView`-based to avoid std::string at the edge.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::fs
{
    struct PathView
    {
        const char* data = nullptr; // Non-owning, not null-terminated guaranteed.
        dng::u32    size = 0;       // Number of bytes in the view.
    };

    static_assert(std::is_trivially_copyable_v<PathView>, "PathView must stay POD for cheap forwarding.");

    enum class FsStatus : dng::u8
    {
        Ok = 0,
        NotFound,
        AccessDenied,
        InvalidArg,
        NotSupported,
        UnknownError
    };

    struct FileSystemCaps
    {
        dng::DeterminismMode determinism = dng::DeterminismMode::Replay;
        dng::ThreadSafetyMode threadSafety = dng::ThreadSafetyMode::ExternalSync;
        bool stableOrderingRequired = true;
    };

    static_assert(std::is_trivially_copyable_v<FileSystemCaps>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    struct FileSystemVTable
    {
        using ExistsFunc   = FsStatus(*)(void* userData, PathView path) noexcept;
        using FileSizeFunc = FsStatus(*)(void* userData, PathView path, dng::u64& outSize) noexcept;
        using ReadFileFunc = FsStatus(*)(void* userData, PathView path, void* dst, dng::u64 dstSize, dng::u64& outRead) noexcept;
        using GetCapsFunc  = FileSystemCaps(*)(const void* userData) noexcept;

        ExistsFunc   exists   = nullptr;
        FileSizeFunc fileSize = nullptr;
        ReadFileFunc readFile = nullptr;
        GetCapsFunc  getCaps  = nullptr;
    };

    struct FileSystemInterface
    {
        FileSystemVTable vtable{};
        void*            userData = nullptr; // Non-owning backend instance pointer.
    };

    [[nodiscard]] inline FileSystemCaps QueryCaps(const FileSystemInterface& fs) noexcept
    {
        return (fs.vtable.getCaps && fs.userData)
            ? fs.vtable.getCaps(fs.userData)
            : FileSystemCaps{};
    }

    [[nodiscard]] inline FsStatus Exists(FileSystemInterface& fs, PathView path) noexcept
    {
        return (fs.vtable.exists && fs.userData)
            ? fs.vtable.exists(fs.userData, path)
            : FsStatus::InvalidArg;
    }

    [[nodiscard]] inline FsStatus FileSize(FileSystemInterface& fs, PathView path, dng::u64& outSize) noexcept
    {
        outSize = 0;
        return (fs.vtable.fileSize && fs.userData)
            ? fs.vtable.fileSize(fs.userData, path, outSize)
            : FsStatus::InvalidArg;
    }

    [[nodiscard]] inline FsStatus ReadFile(FileSystemInterface& fs, PathView path, void* dst, dng::u64 dstSize, dng::u64& outRead) noexcept
    {
        outRead = 0;
        return (fs.vtable.readFile && fs.userData)
            ? fs.vtable.readFile(fs.userData, path, dst, dstSize, outRead)
            : FsStatus::InvalidArg;
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    template <typename Backend>
    concept FileSystemBackend = requires(Backend& backend,
                                         const Backend& constBackend,
                                         const PathView path,
                                         void* dst,
                                         dng::u64 dstSize,
                                         dng::u64& outSize,
                                         dng::u64& outRead)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<FileSystemCaps>;
        { backend.Exists(path) } noexcept -> std::same_as<FsStatus>;
        { backend.FileSize(path, outSize) } noexcept -> std::same_as<FsStatus>;
        { backend.ReadFile(path, dst, dstSize, outRead) } noexcept -> std::same_as<FsStatus>;
    };

    namespace detail
    {
        template <typename Backend>
        struct FileSystemInterfaceAdapter
        {
            static FileSystemCaps GetCaps(const void* userData) noexcept
            {
                return static_cast<const Backend*>(userData)->GetCaps();
            }

            static FsStatus Exists(void* userData, PathView path) noexcept
            {
                return static_cast<Backend*>(userData)->Exists(path);
            }

            static FsStatus FileSize(void* userData, PathView path, dng::u64& outSize) noexcept
            {
                return static_cast<Backend*>(userData)->FileSize(path, outSize);
            }

            static FsStatus ReadFile(void* userData, PathView path, void* dst, dng::u64 dstSize, dng::u64& outRead) noexcept
            {
                return static_cast<Backend*>(userData)->ReadFile(path, dst, dstSize, outRead);
            }
        };
    } // namespace detail

    template <typename Backend>
    [[nodiscard]] inline FileSystemInterface MakeFileSystemInterface(Backend& backend) noexcept
    {
        static_assert(FileSystemBackend<Backend>, "Backend must satisfy FileSystemBackend concept.");

        FileSystemInterface iface{};
        iface.userData         = &backend;
        iface.vtable.getCaps   = &detail::FileSystemInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.exists    = &detail::FileSystemInterfaceAdapter<Backend>::Exists;
        iface.vtable.fileSize  = &detail::FileSystemInterfaceAdapter<Backend>::FileSize;
        iface.vtable.readFile  = &detail::FileSystemInterfaceAdapter<Backend>::ReadFile;
        return iface;
    }

} // namespace dng::fs
