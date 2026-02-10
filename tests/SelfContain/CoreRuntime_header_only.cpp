#include "Core/Runtime/CoreRuntime.hpp"

namespace
{
    int CoreRuntimeHeaderOnly()
    {
        dng::runtime::CoreRuntimeState state{};
        dng::runtime::CoreRuntimeConfig config{};
        dng::runtime::CoreRuntimeInjectedInterfaces injected{};
        dng::runtime::CoreRuntimeTickParams tickParams{};
        dng::audio::AudioMixParams audioMix{};
        tickParams.audioMix = &audioMix;

        const dng::runtime::CoreRuntimeTickResult tickResult =
            dng::runtime::TickCoreRuntime(state, tickParams);
        (void)tickResult;

        dng::runtime::CoreRuntimeScope scope(state, config, injected);
        (void)scope;

        (void)dng::runtime::IsInitialized(state);
        (void)dng::runtime::GetInitStage(state);
        return 0;
    }
}
