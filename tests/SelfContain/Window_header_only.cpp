#include "Core/Contracts/Window.hpp"
#include "Core/Window/NullWindow.hpp"

namespace
{
    using namespace dng::win;

    static_assert(WindowBackend<NullWindow>, "NullWindow must satisfy window backend concept.");

    struct DummyWindow
    {
        [[nodiscard]] constexpr WindowCaps GetCaps() const noexcept { return {}; }
        [[nodiscard]] WindowStatus CreateWindow(const WindowDesc&, WindowHandle& outHandle) noexcept { outHandle = WindowHandle{42}; return WindowStatus::Ok; }
        [[nodiscard]] WindowStatus DestroyWindow(WindowHandle) noexcept { return WindowStatus::Ok; }
        [[nodiscard]] WindowStatus PollEvents(WindowEvent*, dng::u32, dng::u32& outCount) noexcept { outCount = 0; return WindowStatus::Ok; }
        [[nodiscard]] WindowStatus GetSurfaceSize(WindowHandle, dng::u32& outWidth, dng::u32& outHeight) noexcept { outWidth = 1; outHeight = 1; return WindowStatus::Ok; }
    };

    static_assert(WindowBackend<DummyWindow>, "DummyWindow must satisfy window backend concept.");

    void UseWindowInterface() noexcept
    {
        DummyWindow backend{};
        auto iface = MakeWindowInterface(backend);
        WindowDesc desc{};
        WindowHandle handle{};
        WindowEvent events[2]{};
        dng::u32 count = 0;
        dng::u32 w = 0;
        dng::u32 h = 0;
        (void)CreateWindow(iface, desc, handle);
        (void)PollEvents(iface, events, 2, count);
        (void)GetSurfaceSize(iface, handle, w, h);
        (void)DestroyWindow(iface, handle);
    }
}
