// ============================================================================
// D-Engine - Source/Core/FileSystem/NullFileSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal file system backend that satisfies the contract without
//           performing any disk access. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations. All methods are
//           noexcept and deterministic.
// Notes   : Always reports NotFound; does not touch provided buffers.
// ============================================================================

#pragma once

#include "Core/Contracts/FileSystem.hpp"

namespace dng::fs
{
    struct NullFileSystem
    {
        [[nodiscard]] constexpr FileSystemCaps GetCaps() const noexcept
        {
            FileSystemCaps caps{};
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableOrderingRequired = true;
            return caps;
        }

        [[nodiscard]] FsStatus Exists(PathView) noexcept
        {
            return FsStatus::NotFound;
        }

        [[nodiscard]] FsStatus FileSize(PathView, dng::u64& outSize) noexcept
        {
            outSize = 0;
            return FsStatus::NotFound;
        }

        [[nodiscard]] FsStatus ReadFile(PathView, void*, dng::u64, dng::u64& outRead) noexcept
        {
            outRead = 0;
            return FsStatus::NotFound;
        }

        [[nodiscard]] FsStatus ReadFileRange(PathView,
                                             dng::u64,
                                             void*,
                                             dng::u64,
                                             dng::u64& outRead) noexcept
        {
            outRead = 0;
            return FsStatus::NotFound;
        }
    };

    static_assert(FileSystemBackend<NullFileSystem>, "NullFileSystem must satisfy file system backend concept.");

    [[nodiscard]] inline FileSystemInterface MakeNullFileSystemInterface(NullFileSystem& backend) noexcept
    {
        return MakeFileSystemInterface(backend);
    }

} // namespace dng::fs
