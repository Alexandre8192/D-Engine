#include "Core/Input/InputSystem.hpp"

int RunInputSmoke()
{
    using namespace dng::input;

    const auto isReset = [](const InputSystemState& state) noexcept
    {
        const InputCaps caps = QueryCaps(state);
        return !state.isInitialized &&
               state.backend == InputSystemBackend::Null &&
               caps.determinism == dng::DeterminismMode::Unknown &&
               caps.threadSafety == dng::ThreadSafetyMode::Unknown &&
               !caps.stableEventOrder;
    };

    InputSystemState uninitialized{};
    if (!isReset(uninitialized))
    {
        return 4;
    }

    InputSystemConfig config{};

    NullInput nullBackendForValidation{};
    InputInterface brokenInterface = MakeNullInputInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    InputSystemState rejected{};
    if (!InitInputSystem(rejected, config))
    {
        return 5;
    }
    if (InitInputSystemWithInterface(rejected, brokenInterface))
    {
        return 6;
    }
    if (!isReset(rejected))
    {
        return 7;
    }

    InputSystemConfig rejectedConfig{};
    rejectedConfig.backend = InputSystemBackend::External;
    if (!InitInputSystem(rejected, config))
    {
        return 8;
    }
    if (InitInputSystem(rejected, rejectedConfig))
    {
        return 9;
    }
    if (!isReset(rejected))
    {
        return 10;
    }

    InputSystemState state{};
    if (!InitInputSystem(state, config))
    {
        return 1;
    }

    const InputCaps caps = QueryCaps(state);
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
