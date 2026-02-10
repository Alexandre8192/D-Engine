#include "Core/Contracts/FileSystem.hpp"
#include "Core/FileSystem/NullFileSystem.hpp"

namespace
{
    using namespace dng::fs;

    static_assert(FileSystemBackend<NullFileSystem>, "NullFileSystem must satisfy file system backend concept.");

    struct DummyFs
    {
        [[nodiscard]] constexpr FileSystemCaps GetCaps() const noexcept { return {}; }
        [[nodiscard]] FsStatus Exists(PathView) noexcept { return FsStatus::Ok; }
        [[nodiscard]] FsStatus FileSize(PathView, dng::u64& outSize) noexcept { outSize = 0; return FsStatus::Ok; }
        [[nodiscard]] FsStatus ReadFile(PathView, void*, dng::u64, dng::u64& outRead) noexcept { outRead = 0; return FsStatus::Ok; }
        [[nodiscard]] FsStatus ReadFileRange(PathView, dng::u64, void*, dng::u64, dng::u64& outRead) noexcept
        {
            outRead = 0;
            return FsStatus::Ok;
        }
    };

    static_assert(FileSystemBackend<DummyFs>, "DummyFs must satisfy file system backend concept.");

    void UseFileSystemInterface() noexcept
    {
        DummyFs backend{};
        auto iface = MakeFileSystemInterface(backend);
        PathView path{"/dev/null", 9};
        dng::u64 size = 0;
        dng::u64 read = 0;
        (void)Exists(iface, path);
        (void)FileSize(iface, path, size);
        (void)ReadFile(iface, path, nullptr, 0, read);
        (void)ReadFileRange(iface, path, 0, nullptr, 0, read);
    }
}
