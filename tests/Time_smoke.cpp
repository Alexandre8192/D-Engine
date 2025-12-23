#include "Core/Time/TimeSystem.hpp"

int RunTimeSmoke()
{
    using namespace dng::time;

    TimeSystemState state{};
    TimeSystemConfig config{};

    if (!InitTimeSystem(state, config))
    {
        return 1;
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
