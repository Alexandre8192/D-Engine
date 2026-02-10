#include "Core/Runtime/CoreRuntime.hpp"

namespace
{
    [[nodiscard]] bool IsRuntimeReset(const dng::runtime::CoreRuntimeState& state) noexcept
    {
        return !state.isInitialized &&
               state.stage == dng::runtime::CoreRuntimeInitStage::None &&
               !state.ownsMemorySystem &&
               !state.time.isInitialized &&
               !state.jobs.isInitialized &&
               !state.input.isInitialized &&
               !state.window.isInitialized &&
               !state.fileSystem.isInitialized &&
               !state.audio.isInitialized &&
               !state.renderer.isInitialized;
    }

    void IncrementJob(void* userData) noexcept
    {
        int* value = static_cast<int*>(userData);
        if (value != nullptr)
        {
            ++(*value);
        }
    }
}

int RunCoreRuntimeSmoke()
{
    using namespace dng::runtime;

    if (dng::memory::MemorySystem::IsInitialized())
    {
        return 1;
    }

    CoreRuntimeState coldState{};
    const CoreRuntimeTickResult coldTick = TickCoreRuntime(coldState);
    if (coldTick.status != CoreRuntimeTickStatus::NotInitialized)
    {
        return 2;
    }

    CoreRuntimeState state{};
    if (IsInitialized(state) || GetInitStage(state) != CoreRuntimeInitStage::None)
    {
        return 3;
    }

    const CoreRuntimeConfig config{};
    if (InitCoreRuntime(state, config) != CoreRuntimeStatus::Ok)
    {
        ShutdownCoreRuntime(state);
        return 4;
    }

    if (!IsInitialized(state) || GetInitStage(state) != CoreRuntimeInitStage::Ready)
    {
        ShutdownCoreRuntime(state);
        return 5;
    }

    if (!dng::memory::MemorySystem::IsInitialized())
    {
        ShutdownCoreRuntime(state);
        return 6;
    }

    if (!state.time.isInitialized ||
        !state.jobs.isInitialized ||
        !state.input.isInitialized ||
        !state.window.isInitialized ||
        !state.fileSystem.isInitialized ||
        !state.audio.isInitialized ||
        !state.renderer.isInitialized)
    {
        ShutdownCoreRuntime(state);
        return 7;
    }

    int jobValue = 0;
    dng::jobs::JobDesc job{};
    job.func = &IncrementJob;
    job.userData = &jobValue;
    dng::jobs::JobCounter waitCounter{};
    dng::jobs::SubmitJob(state.jobs, job, waitCounter);

    dng::render::RenderView view{};
    view.width = 640;
    view.height = 360;
    dng::render::FrameSubmission submission{};
    submission.views = &view;
    submission.viewCount = 1;

    dng::input::InputEvent inputEvents[4]{};
    float audioBuffer[64]{};
    for (float& sample : audioBuffer)
    {
        sample = 1.0f;
    }

    dng::audio::AudioMixParams audioMix{};
    audioMix.outSamples = audioBuffer;
    audioMix.outputCapacitySamples = 64;
    audioMix.sampleRate = 48000;
    audioMix.channelCount = 2;
    audioMix.requestedFrames = 32;

    CoreRuntimeTickParams tickParams{};
    tickParams.inputEvents = inputEvents;
    tickParams.inputCapacity = 4;
    tickParams.audioMix = &audioMix;
    tickParams.frameSubmission = &submission;
    tickParams.waitCounter = &waitCounter;
    const CoreRuntimeTickResult tick = TickCoreRuntime(state, tickParams);

    if (tick.status != CoreRuntimeTickStatus::Ok ||
        tick.inputStatus != dng::input::InputStatus::Ok ||
        tick.audioStatus != dng::audio::AudioStatus::Ok ||
        tick.inputEventCount != 0 ||
        tick.frame.frameIndex == 0 ||
        !waitCounter.IsComplete() ||
        jobValue != 1)
    {
        ShutdownCoreRuntime(state);
        return 8;
    }

    if (audioMix.writtenSamples != 64 ||
        audioMix.frameIndex != tick.frame.frameIndex ||
        state.audio.nullBackend.lastFrameIndex != tick.frame.frameIndex)
    {
        ShutdownCoreRuntime(state);
        return 9;
    }

    for (dng::u32 i = 0; i < audioMix.writtenSamples; ++i)
    {
        if (audioBuffer[i] != 0.0f)
        {
            ShutdownCoreRuntime(state);
            return 10;
        }
    }

    if (state.renderer.nullBackend.width != 640 || state.renderer.nullBackend.height != 360)
    {
        ShutdownCoreRuntime(state);
        return 11;
    }

    if (InitCoreRuntime(state, config) != CoreRuntimeStatus::AlreadyInitialized)
    {
        ShutdownCoreRuntime(state);
        return 12;
    }

    ShutdownCoreRuntime(state);
    if (!IsRuntimeReset(state) || dng::memory::MemorySystem::IsInitialized())
    {
        return 13;
    }

    CoreRuntimeState scopedState{};
    {
        CoreRuntimeScope scope(scopedState, config);
        if (scope.GetStatus() != CoreRuntimeStatus::Ok || !scope.OwnsLifetime())
        {
            return 14;
        }

        if (!scopedState.isInitialized || !dng::memory::MemorySystem::IsInitialized())
        {
            return 15;
        }
    }

    if (!IsRuntimeReset(scopedState) || dng::memory::MemorySystem::IsInitialized())
    {
        return 16;
    }

    {
        CoreRuntimeState injectedState{};

        dng::time::NullTime timeBackend{};
        dng::jobs::NullJobs jobsBackend{};
        dng::input::NullInput inputBackend{};
        dng::win::NullWindow windowBackend{};
        dng::fs::NullFileSystem fileSystemBackend{};
        dng::audio::NullAudio audioBackend{};
        dng::render::NullRenderer rendererBackend{};

        const dng::time::TimeInterface timeIface = dng::time::MakeNullTimeInterface(timeBackend);
        const dng::jobs::JobsInterface jobsIface = dng::jobs::MakeNullJobsInterface(jobsBackend);
        const dng::input::InputInterface inputIface = dng::input::MakeNullInputInterface(inputBackend);
        const dng::win::WindowInterface windowIface = dng::win::MakeNullWindowInterface(windowBackend);
        const dng::fs::FileSystemInterface fileSystemIface = dng::fs::MakeNullFileSystemInterface(fileSystemBackend);
        const dng::audio::AudioInterface audioIface = dng::audio::MakeNullAudioInterface(audioBackend);
        const dng::render::RendererInterface rendererIface = dng::render::MakeNullRendererInterface(rendererBackend);

        CoreRuntimeInjectedInterfaces injected{};
        injected.timeSystem = &timeIface;
        injected.jobsSystem = &jobsIface;
        injected.inputSystem = &inputIface;
        injected.windowSystem = &windowIface;
        injected.fileSystem = &fileSystemIface;
        injected.audioSystem = &audioIface;
        injected.audioBackend = dng::audio::AudioSystemBackend::External;
        injected.rendererSystem = &rendererIface;
        injected.rendererBackend = dng::render::RendererSystemBackend::Null;

        if (InitCoreRuntime(injectedState, config, injected) != CoreRuntimeStatus::Ok)
        {
            ShutdownCoreRuntime(injectedState);
            return 17;
        }

        if (injectedState.time.backend != dng::time::TimeSystemBackend::External ||
            injectedState.jobs.backend != dng::jobs::JobsSystemBackend::External ||
            injectedState.input.backend != dng::input::InputSystemBackend::External ||
            injectedState.window.backend != dng::win::WindowSystemBackend::External ||
            injectedState.fileSystem.backend != dng::fs::FileSystemSystemBackend::External ||
            injectedState.audio.backend != dng::audio::AudioSystemBackend::External ||
            injectedState.renderer.backend != dng::render::RendererSystemBackend::Null)
        {
            ShutdownCoreRuntime(injectedState);
            return 18;
        }

        ShutdownCoreRuntime(injectedState);
        if (!IsRuntimeReset(injectedState) || dng::memory::MemorySystem::IsInitialized())
        {
            return 19;
        }
    }

    auto expectFailureReset = [&](CoreRuntimeStatus expectedStatus,
                                  const CoreRuntimeConfig& expectedConfig,
                                  const CoreRuntimeInjectedInterfaces& expectedInjected,
                                  int errorCode) -> int
    {
        CoreRuntimeState failedState{};
        const CoreRuntimeStatus failedStatus = InitCoreRuntime(failedState, expectedConfig, expectedInjected);
        if (failedStatus != expectedStatus)
        {
            ShutdownCoreRuntime(failedState);
            return errorCode;
        }

        if (!IsRuntimeReset(failedState) || dng::memory::MemorySystem::IsInitialized())
        {
            ShutdownCoreRuntime(failedState);
            return errorCode + 1;
        }

        return 0;
    };

    {
        dng::time::NullTime badBackend{};
        dng::time::TimeInterface badIface = dng::time::MakeNullTimeInterface(badBackend);
        badIface.vtable.getCaps = nullptr;

        CoreRuntimeInjectedInterfaces injected{};
        injected.timeSystem = &badIface;
        const int result = expectFailureReset(CoreRuntimeStatus::TimeInitFailed, config, injected, 20);
        if (result != 0)
        {
            return result;
        }
    }

    {
        dng::jobs::NullJobs badBackend{};
        dng::jobs::JobsInterface badIface = dng::jobs::MakeNullJobsInterface(badBackend);
        badIface.vtable.getCaps = nullptr;

        CoreRuntimeInjectedInterfaces injected{};
        injected.jobsSystem = &badIface;
        const int result = expectFailureReset(CoreRuntimeStatus::JobsInitFailed, config, injected, 22);
        if (result != 0)
        {
            return result;
        }
    }

    {
        dng::input::NullInput badBackend{};
        dng::input::InputInterface badIface = dng::input::MakeNullInputInterface(badBackend);
        badIface.vtable.getCaps = nullptr;

        CoreRuntimeInjectedInterfaces injected{};
        injected.inputSystem = &badIface;
        const int result = expectFailureReset(CoreRuntimeStatus::InputInitFailed, config, injected, 24);
        if (result != 0)
        {
            return result;
        }
    }

    {
        dng::win::NullWindow badBackend{};
        dng::win::WindowInterface badIface = dng::win::MakeNullWindowInterface(badBackend);
        badIface.vtable.getCaps = nullptr;

        CoreRuntimeInjectedInterfaces injected{};
        injected.windowSystem = &badIface;
        const int result = expectFailureReset(CoreRuntimeStatus::WindowInitFailed, config, injected, 26);
        if (result != 0)
        {
            return result;
        }
    }

    {
        dng::fs::NullFileSystem badBackend{};
        dng::fs::FileSystemInterface badIface = dng::fs::MakeNullFileSystemInterface(badBackend);
        badIface.vtable.getCaps = nullptr;

        CoreRuntimeInjectedInterfaces injected{};
        injected.fileSystem = &badIface;
        const int result = expectFailureReset(CoreRuntimeStatus::FileSystemInitFailed, config, injected, 28);
        if (result != 0)
        {
            return result;
        }
    }

    {
        dng::audio::NullAudio badBackend{};
        dng::audio::AudioInterface badIface = dng::audio::MakeNullAudioInterface(badBackend);
        badIface.vtable.getCaps = nullptr;

        CoreRuntimeInjectedInterfaces injected{};
        injected.audioSystem = &badIface;
        injected.audioBackend = dng::audio::AudioSystemBackend::External;
        const int result = expectFailureReset(CoreRuntimeStatus::AudioInitFailed, config, injected, 30);
        if (result != 0)
        {
            return result;
        }
    }

    {
        CoreRuntimeConfig rendererFailConfig = config;
        rendererFailConfig.renderer.backend = dng::render::RendererSystemBackend::Forward;
        const CoreRuntimeInjectedInterfaces injected{};
        const int result = expectFailureReset(CoreRuntimeStatus::RendererInitFailed, rendererFailConfig, injected, 32);
        if (result != 0)
        {
            return result;
        }
    }

    return 0;
}
