#include "Core/Time/TimeSystem.hpp"

int RunTimeSmoke()
{
    using namespace dng::time;

    TimeSystemState uninitialized{};
    const TimeCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinism != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableSampleOrder)
    {
        return 8;
    }

    NullTime nullBackendForValidation{};
    TimeInterface brokenInterface = MakeNullTimeInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    TimeSystemState rejected{};
    if (InitTimeSystemWithInterface(rejected, brokenInterface, TimeSystemBackend::External))
    {
        return 9;
    }

    TimeSystemState state{};
    TimeSystemConfig config{};

    if (!InitTimeSystem(state, config))
    {
        return 1;
    }

    const TimeCaps caps = QueryCaps(state.interface);
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
