#include "Core/Time/TimeSystem.hpp"

int RunTimeSmoke()
{
    using namespace dng::time;

    const auto isReset = [](const TimeSystemState& state) noexcept
    {
        const TimeCaps caps = QueryCaps(state);
        return !state.isInitialized &&
               state.backend == TimeSystemBackend::Null &&
               state.lastFrameTime.frameIndex == 0U &&
               state.lastFrameTime.deltaNs == 0U &&
               state.lastFrameTime.totalNs == 0U &&
               caps.determinism == dng::DeterminismMode::Unknown &&
               caps.threadSafety == dng::ThreadSafetyMode::Unknown &&
               !caps.stableSampleOrder;
    };

    TimeSystemState uninitialized{};
    if (!isReset(uninitialized))
    {
        return 8;
    }

    TimeSystemConfig config{};

    NullTime nullBackendForValidation{};
    TimeInterface brokenInterface = MakeNullTimeInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    TimeSystemState rejected{};
    if (!InitTimeSystem(rejected, config))
    {
        return 9;
    }
    if (InitTimeSystemWithInterface(rejected, brokenInterface))
    {
        return 10;
    }
    if (!isReset(rejected))
    {
        return 11;
    }

    TimeSystemConfig rejectedConfig{};
    rejectedConfig.backend = TimeSystemBackend::External;
    if (!InitTimeSystem(rejected, config))
    {
        return 12;
    }
    if (InitTimeSystem(rejected, rejectedConfig))
    {
        return 13;
    }
    if (!isReset(rejected))
    {
        return 14;
    }

    TimeSystemState state{};
    if (!InitTimeSystem(state, config))
    {
        return 1;
    }

    const TimeCaps caps = QueryCaps(state);
    if (!caps.monotonic ||
        caps.determinism != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableSampleOrder)
    {
        return 7;
    }

    FrameTime previous = state.lastFrameTime;


    if (previous.frameIndex != 0U || previous.deltaNs != 0U)
    {
        return 2;
    }

    if (previous.totalNs == 0U)
    {
        return 3;
    }

    for (int i = 0; i < 3; ++i)
    {
        FrameTime current = TickTimeSystem(state);

        if (current.frameIndex != previous.frameIndex + 1U)
        {
            return 4;
        }

        if (current.deltaNs == 0U)
        {
            return 5;
        }

        if (current.totalNs <= previous.totalNs)
        {
            return 6;
        }

        previous = current;
    }

    ShutdownTimeSystem(state);
    return 0;
}
