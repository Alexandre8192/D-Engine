#include "Core/Input/InputSystem.hpp"

int RunInputSmoke()
{
    using namespace dng::input;

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
