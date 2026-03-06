#include "Core/Window/WindowSystem.hpp"

int RunWindowSmoke()
{
    using namespace dng::win;

    const auto isReset = [](const WindowSystemState& state) noexcept
    {
        const WindowCaps caps = QueryCaps(state);
        return !state.isInitialized &&
               state.backend == WindowSystemBackend::Null &&
               caps.determinism == dng::DeterminismMode::Unknown &&
               caps.threadSafety == dng::ThreadSafetyMode::Unknown &&
               !caps.stableEventOrder;
    };

    WindowSystemState uninitialized{};
    if (!isReset(uninitialized))
    {
        return 7;
    }

    WindowSystemConfig config{};

    NullWindow nullBackendForValidation{};
    WindowInterface brokenInterface = MakeNullWindowInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    WindowSystemState rejected{};
    if (!InitWindowSystem(rejected, config))
    {
        return 8;
    }
    if (InitWindowSystemWithInterface(rejected, brokenInterface))
    {
        return 9;
    }
    if (!isReset(rejected))
    {
        return 10;
    }

    WindowSystemConfig rejectedConfig{};
    rejectedConfig.backend = WindowSystemBackend::External;
    if (!InitWindowSystem(rejected, config))
    {
        return 11;
    }
    if (InitWindowSystem(rejected, rejectedConfig))
    {
        return 12;
    }
    if (!isReset(rejected))
    {
        return 13;
    }

    WindowSystemState state{};
    if (!InitWindowSystem(state, config))
    {
        return 1;
    }

    const WindowCaps caps = QueryCaps(state);
    if (caps.determinism != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableEventOrder)
    {
        return 6;
    }

    WindowDesc desc{};

    desc.width  = 800;
    desc.height = 600;
    constexpr char titleData[] = "Test";
    desc.title.data = titleData;
    desc.title.size = 4;

    WindowHandle handle{};
    if (CreateWindow(state, desc, handle) != WindowStatus::Ok || !handle.IsValid())
    {
        return 2;
    }

    dng::u32 w = 0;
    dng::u32 h = 0;
    if (GetSurfaceSize(state, handle, w, h) != WindowStatus::Ok || w != desc.width || h != desc.height)
    {
        return 3;
    }

    WindowEvent events[4]{};
    dng::u32 count = 0;
    if (PollEvents(state, events, 4, count) != WindowStatus::Ok || count != 0)
    {
        return 4;
    }

    if (DestroyWindow(state, handle) != WindowStatus::Ok)
    {
        return 5;
    }

    ShutdownWindowSystem(state);
    return 0;
}
