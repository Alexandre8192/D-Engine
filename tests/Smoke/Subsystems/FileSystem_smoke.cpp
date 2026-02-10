#include "Core/FileSystem/FileSystemSystem.hpp"

int RunFileSystemSmoke()
{
    using namespace dng::fs;

    FileSystemSystemState uninitialized{};
    const FileSystemCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinism != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableOrderingRequired)
    {
        return 6;
    }

    NullFileSystem nullBackendForValidation{};
    FileSystemInterface brokenInterface = MakeNullFileSystemInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    FileSystemSystemState rejected{};
    if (InitFileSystemSystemWithInterface(rejected, brokenInterface, FileSystemSystemBackend::External))
    {
        return 7;
    }

    FileSystemSystemState state{};
    FileSystemSystemConfig config{};

    if (!InitFileSystemSystem(state, config))
    {
        return 1;
    }

    const FileSystemCaps caps = QueryCaps(state.interface);
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

    ShutdownFileSystemSystem(state);
    return 0;
}
