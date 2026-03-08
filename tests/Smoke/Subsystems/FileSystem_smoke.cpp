#include "Core/FileSystem/FileSystemSystem.hpp"

int RunFileSystemSmoke()
{
    using namespace dng::fs;

    const auto isReset = [](const FileSystemSystemState& state) noexcept
    {
        const FileSystemCaps caps = QueryCaps(state);
        return !state.isInitialized &&
               state.backend == FileSystemSystemBackend::Null &&
               caps.determinism == dng::DeterminismMode::Unknown &&
               caps.threadSafety == dng::ThreadSafetyMode::Unknown &&
               !caps.stableOrderingRequired;
    };

    FileSystemSystemState uninitialized{};
    if (!isReset(uninitialized))
    {
        return 6;
    }

    FileSystemSystemConfig config{};

    NullFileSystem nullBackendForValidation{};
    FileSystemInterface brokenInterface = MakeNullFileSystemInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    FileSystemSystemState rejected{};
    if (!InitFileSystemSystem(rejected, config))
    {
        return 7;
    }
    if (InitFileSystemSystemWithInterface(rejected, brokenInterface))
    {
        return 8;
    }
    if (!isReset(rejected))
    {
        return 9;
    }

    FileSystemSystemConfig rejectedConfig{};
    rejectedConfig.backend = FileSystemSystemBackend::External;
    if (!InitFileSystemSystem(rejected, config))
    {
        return 10;
    }
    if (InitFileSystemSystem(rejected, rejectedConfig))
    {
        return 11;
    }
    if (!isReset(rejected))
    {
        return 12;
    }

    FileSystemSystemState state{};
    if (!InitFileSystemSystem(state, config))
    {
        return 1;
    }

    const FileSystemCaps caps = QueryCaps(state);
    if (caps.determinism != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableOrderingRequired)
    {
        return 8;
    }

    constexpr char pathData[] = "dummy.txt";
    PathView path{pathData, static_cast<dng::u32>(sizeof(pathData) - 1U)};

    dng::u64 size = 0;
    dng::u64 read = 0;
    char buffer[4]{};


    if (Exists(state, path) != FsStatus::NotFound)
    {
        return 2;
    }

    if (FileSize(state, path, size) != FsStatus::NotFound || size != 0)
    {
        return 3;
    }

    if (ReadFile(state, path, buffer, sizeof(buffer), read) != FsStatus::NotFound || read != 0)
    {
        return 4;
    }

    if (ReadFileRange(state, path, 2, buffer, sizeof(buffer), read) != FsStatus::NotFound || read != 0)
    {
        return 5;
    }

    ShutdownFileSystemSystem(state);
    return 0;
}
