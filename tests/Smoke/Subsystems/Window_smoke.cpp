#include "Core/Window/WindowSystem.hpp"

int RunWindowSmoke()
{
    using namespace dng::win;

    WindowSystemState uninitialized{};
    const WindowCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinism != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableEventOrder)
    {
        return 7;
    }

    NullWindow nullBackendForValidation{};
    WindowInterface brokenInterface = MakeNullWindowInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    WindowSystemState rejected{};
    if (InitWindowSystemWithInterface(rejected, brokenInterface, WindowSystemBackend::External))
    {
        return 8;
    }

    WindowSystemState state{};
    WindowSystemConfig config{};

    if (!InitWindowSystem(state, config))
    {
        return 1;
    }

    const WindowCaps caps = QueryCaps(state.interface);
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
