#include "Core/Input/InputSystem.hpp"

int RunInputSmoke()
{
    using namespace dng::input;

    InputSystemState uninitialized{};
    const InputCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinism != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableEventOrder)
    {
        return 4;
    }

    NullInput nullBackendForValidation{};
    InputInterface brokenInterface = MakeNullInputInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    InputSystemState rejected{};
    if (InitInputSystemWithInterface(rejected, brokenInterface, InputSystemBackend::External))
    {
        return 5;
    }

    InputSystemState state{};
    InputSystemConfig config{};

    if (!InitInputSystem(state, config))
    {
        return 1;
    }

    const InputCaps caps = QueryCaps(state.interface);
    if (caps.determinism != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableEventOrder)
    {
        return 3;
    }

    InputEvent events[4]{};
    dng::u32 count = 0;

    if (PollEvents(state, events, 4, count) != InputStatus::Ok || count != 0)
    {
        return 2;
    }


    ShutdownInputSystem(state);
    return 0;
}
