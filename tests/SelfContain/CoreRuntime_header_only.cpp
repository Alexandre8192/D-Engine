#include "Core/Runtime/CoreRuntime.hpp"

namespace
{
    int CoreRuntimeHeaderOnly()
    {
        dng::runtime::CoreRuntimeState state{};
        dng::runtime::CoreRuntimeConfig config{};
        (void)config;
        (void)dng::runtime::IsInitialized(state);
        (void)dng::runtime::GetInitStage(state);
        return 0;
    }
}
