// ============================================================================
// D-Engine - Source/Core/Runtime/CoreRuntime.cpp
// ----------------------------------------------------------------------------
// Purpose : Implement CoreRuntime lifecycle orchestration outside the public
//           header so include costs stay bounded for callers.
// Contract: No exceptions/RTTI; deterministic init/shutdown order; rollback on
//           failure leaves state reset.
// Notes   : The public header remains the only API surface; this file owns the
//           orchestration details and small runtime-only helpers.
// ============================================================================

#include "Core/Runtime/CoreRuntime.hpp"

namespace dng::runtime
{
    namespace
    {
        constexpr double kNanosecondsToSeconds = 1.0 / 1000000000.0;

        [[nodiscard]] inline float NanosecondsToSecondsF(time::Nanoseconds value) noexcept
        {
            return static_cast<float>(static_cast<double>(value) * kNanosecondsToSeconds);
        }
    } // namespace

    void ShutdownCoreRuntime(CoreRuntimeState& state) noexcept
    {
        render::ShutdownRendererSystem(state.renderer);
        audio::ShutdownAudioSystem(state.audio);
        fs::ShutdownFileSystemSystem(state.fileSystem);
        win::ShutdownWindowSystem(state.window);
        input::ShutdownInputSystem(state.input);
        jobs::ShutdownJobsSystem(state.jobs);
        time::ShutdownTimeSystem(state.time);

        if (state.ownsMemorySystem && memory::MemorySystem::IsInitialized())
        {
            memory::MemorySystem::Shutdown();
        }

        state = CoreRuntimeState{};
    }

    [[nodiscard]] CoreRuntimeStatus InitCoreRuntime(CoreRuntimeState& state,
                                                    const CoreRuntimeConfig& config,
                                                    const CoreRuntimeInjectedInterfaces& injected) noexcept
    {
        if (state.isInitialized)
        {
            return CoreRuntimeStatus::AlreadyInitialized;
        }

        state = CoreRuntimeState{};

        state.ownsMemorySystem = !memory::MemorySystem::IsInitialized();
        if (!state.ownsMemorySystem && !memory::MemorySystem::IsConfigCompatible(config.memory))
        {
            state = CoreRuntimeState{};
            return CoreRuntimeStatus::MemoryConfigConflict;
        }

        memory::MemorySystem::Init(config.memory);
        state.stage = CoreRuntimeInitStage::Memory;

        const bool initTimeOk = (injected.timeSystem != nullptr)
            ? time::InitTimeSystemWithInterface(state.time,
                                                *injected.timeSystem,
                                                config.time.primeOnInit)
            : time::InitTimeSystem(state.time, config.time);
        if (!initTimeOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::TimeInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Time;

        const bool initJobsOk = (injected.jobsSystem != nullptr)
            ? jobs::InitJobsSystemWithInterface(state.jobs, *injected.jobsSystem)
            : jobs::InitJobsSystem(state.jobs, config.jobs);
        if (!initJobsOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::JobsInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Jobs;

        const bool initInputOk = (injected.inputSystem != nullptr)
            ? input::InitInputSystemWithInterface(state.input, *injected.inputSystem)
            : input::InitInputSystem(state.input, config.input);
        if (!initInputOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::InputInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Input;

        const bool initWindowOk = (injected.windowSystem != nullptr)
            ? win::InitWindowSystemWithInterface(state.window, *injected.windowSystem)
            : win::InitWindowSystem(state.window, config.window);
        if (!initWindowOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::WindowInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Window;

        const bool initFileSystemOk = (injected.fileSystem != nullptr)
            ? fs::InitFileSystemSystemWithInterface(state.fileSystem, *injected.fileSystem)
            : fs::InitFileSystemSystem(state.fileSystem, config.fileSystem);
        if (!initFileSystemOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::FileSystemInitFailed;
        }
        state.stage = CoreRuntimeInitStage::FileSystem;

        const bool initAudioOk = (injected.audioSystem != nullptr)
            ? audio::InitAudioSystemWithInterface(state.audio, *injected.audioSystem)
            : audio::InitAudioSystem(state.audio, config.audio);
        if (!initAudioOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::AudioInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Audio;

        const bool initRendererOk = (injected.rendererSystem != nullptr)
            ? render::InitRendererSystemWithInterface(state.renderer, *injected.rendererSystem)
            : render::InitRendererSystem(state.renderer, config.renderer);
        if (!initRendererOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::RendererInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Renderer;

        state.isInitialized = true;
        state.stage = CoreRuntimeInitStage::Ready;
        return CoreRuntimeStatus::Ok;
    }

    [[nodiscard]] CoreRuntimeTickResult TickCoreRuntime(CoreRuntimeState& state,
                                                        const CoreRuntimeTickParams& params) noexcept
    {
        CoreRuntimeTickResult result{};
        if (!state.isInitialized)
        {
            return result;
        }

        result.frame = time::TickTimeSystem(state.time);
        result.inputStatus = input::PollEvents(state.input,
                                               params.inputEvents,
                                               params.inputCapacity,
                                               result.inputEventCount);

        if (params.waitCounter != nullptr)
        {
            jobs::WaitForCounter(state.jobs, *params.waitCounter);
        }

        if (params.audioMix != nullptr)
        {
            params.audioMix->frameIndex = result.frame.frameIndex;
            params.audioMix->deltaTimeSec = NanosecondsToSecondsF(result.frame.deltaNs);
            result.audioStatus = audio::Mix(state.audio, *params.audioMix);
        }
        else
        {
            result.audioStatus = audio::AudioStatus::Ok;
        }

        render::FrameSubmission submission{};
        if (params.frameSubmission != nullptr)
        {
            submission = *params.frameSubmission;
        }

        submission.frameIndex = result.frame.frameIndex;
        submission.deltaTimeSec = NanosecondsToSecondsF(result.frame.deltaNs);
        render::RenderFrame(state.renderer, submission);

        result.status = CoreRuntimeTickStatus::Ok;
        return result;
    }

    CoreRuntimeScope::CoreRuntimeScope(CoreRuntimeState& state,
                                       const CoreRuntimeConfig& config,
                                       const CoreRuntimeInjectedInterfaces& injected) noexcept
        : mState(&state)
    {
        const bool wasInitialized = state.isInitialized;
        mStatus = InitCoreRuntime(state, config, injected);
        mOwnsLifetime = (!wasInitialized && mStatus == CoreRuntimeStatus::Ok);
    }

    CoreRuntimeScope::CoreRuntimeScope(CoreRuntimeScope&& other) noexcept
        : mState(other.mState)
        , mStatus(other.mStatus)
        , mOwnsLifetime(other.mOwnsLifetime)
    {
        other.mState = nullptr;
        other.mStatus = CoreRuntimeStatus::Ok;
        other.mOwnsLifetime = false;
    }

    CoreRuntimeScope& CoreRuntimeScope::operator=(CoreRuntimeScope&& other) noexcept
    {
        if (this != &other)
        {
            if (mOwnsLifetime && mState != nullptr)
            {
                ShutdownCoreRuntime(*mState);
            }

            mState = other.mState;
            mStatus = other.mStatus;
            mOwnsLifetime = other.mOwnsLifetime;

            other.mState = nullptr;
            other.mStatus = CoreRuntimeStatus::Ok;
            other.mOwnsLifetime = false;
        }

        return *this;
    }

    CoreRuntimeScope::~CoreRuntimeScope() noexcept
    {
        if (mOwnsLifetime && mState != nullptr)
        {
            ShutdownCoreRuntime(*mState);
        }
    }

    [[nodiscard]] CoreRuntimeStatus CoreRuntimeScope::GetStatus() const noexcept
    {
        return mStatus;
    }

    [[nodiscard]] bool CoreRuntimeScope::OwnsLifetime() const noexcept
    {
        return mOwnsLifetime;
    }
} // namespace dng::runtime
