#include "Core/Runtime/CoreRuntime.hpp"

int RunCoreRuntimeSmoke()
{
    using namespace dng::runtime;

    if (dng::memory::MemorySystem::IsInitialized())
    {
        return 1;
    }

    CoreRuntimeState state{};
    if (IsInitialized(state) || GetInitStage(state) != CoreRuntimeInitStage::None)
    {
        return 2;
    }

    const CoreRuntimeConfig config{};
    if (InitCoreRuntime(state, config) != CoreRuntimeStatus::Ok)
    {
        ShutdownCoreRuntime(state);
        return 3;
    }

    if (!IsInitialized(state) || GetInitStage(state) != CoreRuntimeInitStage::Ready)
    {
        ShutdownCoreRuntime(state);
        return 4;
    }

    if (!dng::memory::MemorySystem::IsInitialized())
    {
        ShutdownCoreRuntime(state);
        return 5;
    }

    if (!state.time.isInitialized ||
        !state.jobs.isInitialized ||
        !state.input.isInitialized ||
        !state.window.isInitialized ||
        !state.fileSystem.isInitialized ||
        !state.renderer.isInitialized)
    {
        ShutdownCoreRuntime(state);
        return 6;
    }

    const dng::time::TimeCaps timeCaps = dng::time::QueryCaps(state.time);
    if (timeCaps.determinism != dng::DeterminismMode::Replay ||
        timeCaps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !timeCaps.stableSampleOrder)
    {
        ShutdownCoreRuntime(state);
        return 7;
    }

    if (InitCoreRuntime(state, config) != CoreRuntimeStatus::AlreadyInitialized)
    {
        ShutdownCoreRuntime(state);
        return 8;
    }

    ShutdownCoreRuntime(state);
    if (IsInitialized(state) || GetInitStage(state) != CoreRuntimeInitStage::None)
    {
        return 9;
    }

    if (dng::memory::MemorySystem::IsInitialized())
    {
        return 10;
    }

    CoreRuntimeState failedState{};
    CoreRuntimeConfig failingConfig{};
    failingConfig.renderer.backend = dng::render::RendererSystemBackend::Forward;
    const CoreRuntimeStatus failedStatus = InitCoreRuntime(failedState, failingConfig);
    if (failedStatus != CoreRuntimeStatus::RendererInitFailed)
    {
        ShutdownCoreRuntime(failedState);
        return 11;
    }

    if (IsInitialized(failedState) || GetInitStage(failedState) != CoreRuntimeInitStage::None)
    {
        return 12;
    }

    if (failedState.time.isInitialized ||
        failedState.jobs.isInitialized ||
        failedState.input.isInitialized ||
        failedState.window.isInitialized ||
        failedState.fileSystem.isInitialized ||
        failedState.renderer.isInitialized)
    {
        return 13;
    }

    if (dng::memory::MemorySystem::IsInitialized())
    {
        return 14;
    }

    return 0;
}
