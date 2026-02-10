// ============================================================================
// D-Engine - Source/Core/FileSystem/FileSystemSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level file system system that owns a backend instance and
//           exposes unified read-only queries to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to FileSystemSystemState.
//           Thread-safety and determinism follow FileSystemCaps from the backend;
//           callers must serialize access per instance.
// Notes   : Defaults to the NullFileSystem backend but accepts external
//           backends via interface injection.
// ============================================================================

#pragma once

#include "Core/Contracts/FileSystem.hpp"
#include "Core/FileSystem/NullFileSystem.hpp"

namespace dng::fs
{
    enum class FileSystemSystemBackend : dng::u8
    {
        Null,
        External
    };

    struct FileSystemSystemConfig
    {
        FileSystemSystemBackend backend = FileSystemSystemBackend::Null;
    };

    struct FileSystemSystemState
    {
        FileSystemInterface        interface{};
        FileSystemSystemBackend    backend       = FileSystemSystemBackend::Null;
        NullFileSystem             nullBackend{};
        bool                       isInitialized = false;
    };

    [[nodiscard]] inline bool InitFileSystemSystemWithInterface(FileSystemSystemState& state,
                                                                FileSystemInterface interface,
                                                                FileSystemSystemBackend backend) noexcept
    {
        if (interface.userData == nullptr ||
            interface.vtable.getCaps == nullptr ||
            interface.vtable.exists == nullptr ||
            interface.vtable.fileSize == nullptr ||
            interface.vtable.readFile == nullptr ||
            interface.vtable.readFileRange == nullptr)
        {
            return false;
        }

        state.interface     = interface;
        state.backend       = backend;
        state.isInitialized = true;
        return true;
    }

    [[nodiscard]] inline bool InitFileSystemSystem(FileSystemSystemState& state,
                                                   const FileSystemSystemConfig& config) noexcept
    {
        state = FileSystemSystemState{};

        switch (config.backend)
        {
            case FileSystemSystemBackend::Null:
            {
                FileSystemInterface iface = MakeNullFileSystemInterface(state.nullBackend);
                return InitFileSystemSystemWithInterface(state, iface, FileSystemSystemBackend::Null);
            }
            case FileSystemSystemBackend::External:
            {
                return false; // Must be injected via InitFileSystemSystemWithInterface.
            }
            default:
            {
                return false;
            }
        }
    }

    inline void ShutdownFileSystemSystem(FileSystemSystemState& state) noexcept
    {
        state.interface     = FileSystemInterface{};
        state.backend       = FileSystemSystemBackend::Null;
        state.nullBackend   = NullFileSystem{};
        state.isInitialized = false;
    }

    [[nodiscard]] inline FileSystemCaps QueryCaps(const FileSystemSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : FileSystemCaps{};
    }

    [[nodiscard]] inline FsStatus Exists(FileSystemSystemState& state, PathView path) noexcept
    {
        if (!state.isInitialized)
        {
            return FsStatus::InvalidArg;
        }
        return Exists(state.interface, path);
    }

    [[nodiscard]] inline FsStatus FileSize(FileSystemSystemState& state, PathView path, dng::u64& outSize) noexcept
    {
        if (!state.isInitialized)
        {
            outSize = 0;
            return FsStatus::InvalidArg;
        }
        return FileSize(state.interface, path, outSize);
    }

    [[nodiscard]] inline FsStatus ReadFile(FileSystemSystemState& state, PathView path, void* dst, dng::u64 dstSize, dng::u64& outRead) noexcept
    {
        if (!state.isInitialized)
        {
            outRead = 0;
            return FsStatus::InvalidArg;
        }
        return ReadFile(state.interface, path, dst, dstSize, outRead);
    }

    [[nodiscard]] inline FsStatus ReadFileRange(FileSystemSystemState& state,
                                                PathView path,
                                                dng::u64 offsetBytes,
                                                void* dst,
                                                dng::u64 dstSize,
                                                dng::u64& outRead) noexcept
    {
        if (!state.isInitialized)
        {
            outRead = 0;
            return FsStatus::InvalidArg;
        }
        return ReadFileRange(state.interface, path, offsetBytes, dst, dstSize, outRead);
    }

} // namespace dng::fs
