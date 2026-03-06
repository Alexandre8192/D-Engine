// ============================================================================
// D-Engine - Source/Core/Runtime/CoreRuntime.hpp
// ----------------------------------------------------------------------------
// Purpose : Orchestrate Core subsystem lifecycle through a single init/shutdown
//           facade so tools and host applications can bootstrap a consistent
//           runtime in one place.
// Contract: Thin public facade, no exceptions/RTTI, deterministic
//           init/shutdown order. Init performs rollback on failure and leaves
//           the state reset. Shutdown is idempotent and only tears down
//           MemorySystem when this runtime instance owns that initialization.
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
        MemoryConfigConflict,
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

    void ShutdownCoreRuntime(CoreRuntimeState& state) noexcept;

    [[nodiscard]] CoreRuntimeStatus InitCoreRuntime(CoreRuntimeState& state,
                                                    const CoreRuntimeConfig& config = {},
                                                    const CoreRuntimeInjectedInterfaces& injected = {}) noexcept;

    [[nodiscard]] CoreRuntimeTickResult TickCoreRuntime(CoreRuntimeState& state,
                                                        const CoreRuntimeTickParams& params = {}) noexcept;

    class CoreRuntimeScope
    {
    public:
        explicit CoreRuntimeScope(CoreRuntimeState& state,
                                  const CoreRuntimeConfig& config = {},
                                  const CoreRuntimeInjectedInterfaces& injected = {}) noexcept;

        CoreRuntimeScope(const CoreRuntimeScope&) = delete;
        CoreRuntimeScope& operator=(const CoreRuntimeScope&) = delete;

        CoreRuntimeScope(CoreRuntimeScope&& other) noexcept;
        CoreRuntimeScope& operator=(CoreRuntimeScope&& other) noexcept;

        ~CoreRuntimeScope() noexcept;

        [[nodiscard]] CoreRuntimeStatus GetStatus() const noexcept;

        [[nodiscard]] bool OwnsLifetime() const noexcept;

    private:
        CoreRuntimeState*  mState = nullptr;
        CoreRuntimeStatus  mStatus = CoreRuntimeStatus::Ok;
        bool               mOwnsLifetime = false;
    };

} // namespace dng::runtime
