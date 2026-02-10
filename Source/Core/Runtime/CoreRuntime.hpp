// ============================================================================
// D-Engine - Source/Core/Runtime/CoreRuntime.hpp
// ----------------------------------------------------------------------------
// Purpose : Orchestrate Core subsystem lifecycle through a single init/shutdown
//           facade so tools and host applications can bootstrap a consistent
//           runtime in one place.
// Contract: Header-only, no exceptions/RTTI, deterministic init/shutdown order.
//           Init performs rollback on failure and leaves the state reset.
//           Shutdown is idempotent and only tears down MemorySystem when this
//           runtime instance owns that initialization.
// Notes   : M0 config-driven path wires built-in subsystem initializers. External
//           interface injection remains available via subsystem-level APIs.
// ============================================================================

#pragma once

#include "Core/Audio/AudioSystem.hpp"
#include "Core/FileSystem/FileSystemSystem.hpp"
#include "Core/Input/InputSystem.hpp"
#include "Core/Jobs/JobsSystem.hpp"
#include "Core/Renderer/RendererSystem.hpp"
#include "Core/Time/TimeSystem.hpp"
#include "Core/Window/WindowSystem.hpp"
#include "Core/Memory/MemorySystem.hpp"

namespace dng::runtime
{
    enum class CoreRuntimeStatus : dng::u8
    {
        Ok = 0,
        AlreadyInitialized,
        TimeInitFailed,
        JobsInitFailed,
        InputInitFailed,
        WindowInitFailed,
        FileSystemInitFailed,
        AudioInitFailed,
        RendererInitFailed
    };

    enum class CoreRuntimeInitStage : dng::u8
    {
        None = 0,
        Memory,
        Time,
        Jobs,
        Input,
        Window,
        FileSystem,
        Audio,
        Renderer,
        Ready
    };

    enum class CoreRuntimeTickStatus : dng::u8
    {
        Ok = 0,
        NotInitialized
    };

    struct CoreRuntimeConfig
    {
        memory::MemoryConfig           memory{};
        time::TimeSystemConfig         time{};
        jobs::JobsSystemConfig         jobs{};
        input::InputSystemConfig       input{};
        win::WindowSystemConfig        window{};
        fs::FileSystemSystemConfig     fileSystem{};
        audio::AudioSystemConfig       audio{};
        render::RendererSystemConfig   renderer{};
    };

    struct CoreRuntimeInjectedInterfaces
    {
        const time::TimeInterface*       timeSystem = nullptr;
        const jobs::JobsInterface*       jobsSystem = nullptr;
        const input::InputInterface*     inputSystem = nullptr;
        const win::WindowInterface*      windowSystem = nullptr;
        const fs::FileSystemInterface*   fileSystem = nullptr;
        const audio::AudioInterface*     audioSystem = nullptr;
        const render::RendererInterface* rendererSystem = nullptr;
        audio::AudioSystemBackend        audioBackend = audio::AudioSystemBackend::External;
        render::RendererSystemBackend    rendererBackend = render::RendererSystemBackend::Forward;
    };

    struct CoreRuntimeState
    {
        CoreRuntimeInitStage       stage = CoreRuntimeInitStage::None;
        bool                       isInitialized = false;
        bool                       ownsMemorySystem = false;
        time::TimeSystemState      time{};
        jobs::JobsSystemState      jobs{};
        input::InputSystemState    input{};
        win::WindowSystemState     window{};
        fs::FileSystemSystemState  fileSystem{};
        audio::AudioSystemState    audio{};
        render::RendererSystemState renderer{};
    };

    struct CoreRuntimeTickParams
    {
        input::InputEvent*              inputEvents = nullptr;
        dng::u32                        inputCapacity = 0;
        audio::AudioMixParams*          audioMix = nullptr;
        const render::FrameSubmission*  frameSubmission = nullptr;
        jobs::JobCounter*               waitCounter = nullptr;
    };

    struct CoreRuntimeTickResult
    {
        CoreRuntimeTickStatus status = CoreRuntimeTickStatus::NotInitialized;
        time::FrameTime       frame{};
        input::InputStatus    inputStatus = input::InputStatus::InvalidArg;
        dng::u32              inputEventCount = 0;
        audio::AudioStatus    audioStatus = audio::AudioStatus::InvalidArg;
    };

    [[nodiscard]] inline bool IsInitialized(const CoreRuntimeState& state) noexcept
    {
        return state.isInitialized;
    }

    [[nodiscard]] inline CoreRuntimeInitStage GetInitStage(const CoreRuntimeState& state) noexcept
    {
        return state.stage;
    }

    inline void ShutdownCoreRuntime(CoreRuntimeState& state) noexcept
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

    [[nodiscard]] inline CoreRuntimeStatus InitCoreRuntime(CoreRuntimeState& state,
                                                           const CoreRuntimeConfig& config = {},
                                                           const CoreRuntimeInjectedInterfaces& injected = {}) noexcept
    {
        if (state.isInitialized)
        {
            return CoreRuntimeStatus::AlreadyInitialized;
        }

        state = CoreRuntimeState{};

        state.ownsMemorySystem = !memory::MemorySystem::IsInitialized();
        memory::MemorySystem::Init(config.memory);
        state.stage = CoreRuntimeInitStage::Memory;

        const bool initTimeOk = (injected.timeSystem != nullptr)
            ? time::InitTimeSystemWithInterface(state.time,
                                                *injected.timeSystem,
                                                time::TimeSystemBackend::External,
                                                config.time.primeOnInit)
            : time::InitTimeSystem(state.time, config.time);
        if (!initTimeOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::TimeInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Time;

        const bool initJobsOk = (injected.jobsSystem != nullptr)
            ? jobs::InitJobsSystemWithInterface(state.jobs,
                                                *injected.jobsSystem,
                                                jobs::JobsSystemBackend::External)
            : jobs::InitJobsSystem(state.jobs, config.jobs);
        if (!initJobsOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::JobsInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Jobs;

        const bool initInputOk = (injected.inputSystem != nullptr)
            ? input::InitInputSystemWithInterface(state.input,
                                                  *injected.inputSystem,
                                                  input::InputSystemBackend::External)
            : input::InitInputSystem(state.input, config.input);
        if (!initInputOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::InputInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Input;

        const bool initWindowOk = (injected.windowSystem != nullptr)
            ? win::InitWindowSystemWithInterface(state.window,
                                                 *injected.windowSystem,
                                                 win::WindowSystemBackend::External)
            : win::InitWindowSystem(state.window, config.window);
        if (!initWindowOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::WindowInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Window;

        const bool initFileSystemOk = (injected.fileSystem != nullptr)
            ? fs::InitFileSystemSystemWithInterface(state.fileSystem,
                                                    *injected.fileSystem,
                                                    fs::FileSystemSystemBackend::External)
            : fs::InitFileSystemSystem(state.fileSystem, config.fileSystem);
        if (!initFileSystemOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::FileSystemInitFailed;
        }
        state.stage = CoreRuntimeInitStage::FileSystem;

        const bool initAudioOk = (injected.audioSystem != nullptr)
            ? audio::InitAudioSystemWithInterface(state.audio,
                                                  *injected.audioSystem,
                                                  injected.audioBackend)
            : audio::InitAudioSystem(state.audio, config.audio);
        if (!initAudioOk)
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::AudioInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Audio;

        const bool initRendererOk = (injected.rendererSystem != nullptr)
            ? render::InitRendererSystemWithInterface(state.renderer,
                                                      *injected.rendererSystem,
                                                      injected.rendererBackend)
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

    [[nodiscard]] inline CoreRuntimeTickResult TickCoreRuntime(CoreRuntimeState& state,
                                                               const CoreRuntimeTickParams& params = {}) noexcept
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
            constexpr double kNanosecondsToSeconds = 1.0 / 1000000000.0;
            params.audioMix->deltaTimeSec = static_cast<float>(
                static_cast<double>(result.frame.deltaNs) * kNanosecondsToSeconds);
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
        constexpr double kNanosecondsToSeconds = 1.0 / 1000000000.0;
        submission.deltaTimeSec = static_cast<float>(
            static_cast<double>(result.frame.deltaNs) * kNanosecondsToSeconds);

        render::RenderFrame(state.renderer, submission);
        result.status = CoreRuntimeTickStatus::Ok;
        return result;
    }

    class CoreRuntimeScope
    {
    public:
        explicit CoreRuntimeScope(CoreRuntimeState& state,
                                  const CoreRuntimeConfig& config = {},
                                  const CoreRuntimeInjectedInterfaces& injected = {}) noexcept
            : mState(&state)
        {
            const bool wasInitialized = state.isInitialized;
            mStatus = InitCoreRuntime(state, config, injected);
            mOwnsLifetime = (!wasInitialized && mStatus == CoreRuntimeStatus::Ok);
        }

        CoreRuntimeScope(const CoreRuntimeScope&) = delete;
        CoreRuntimeScope& operator=(const CoreRuntimeScope&) = delete;

        CoreRuntimeScope(CoreRuntimeScope&& other) noexcept
            : mState(other.mState)
            , mStatus(other.mStatus)
            , mOwnsLifetime(other.mOwnsLifetime)
        {
            other.mState = nullptr;
            other.mStatus = CoreRuntimeStatus::Ok;
            other.mOwnsLifetime = false;
        }

        CoreRuntimeScope& operator=(CoreRuntimeScope&& other) noexcept
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

        ~CoreRuntimeScope() noexcept
        {
            if (mOwnsLifetime && mState != nullptr)
            {
                ShutdownCoreRuntime(*mState);
            }
        }

        [[nodiscard]] CoreRuntimeStatus GetStatus() const noexcept
        {
            return mStatus;
        }

        [[nodiscard]] bool OwnsLifetime() const noexcept
        {
            return mOwnsLifetime;
        }

    private:
        CoreRuntimeState*  mState = nullptr;
        CoreRuntimeStatus  mStatus = CoreRuntimeStatus::Ok;
        bool               mOwnsLifetime = false;
    };

} // namespace dng::runtime
