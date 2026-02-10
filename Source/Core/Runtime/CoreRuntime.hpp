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
        Renderer,
        Ready
    };

    struct CoreRuntimeConfig
    {
        memory::MemoryConfig           memory{};
        time::TimeSystemConfig         time{};
        jobs::JobsSystemConfig         jobs{};
        input::InputSystemConfig       input{};
        win::WindowSystemConfig        window{};
        fs::FileSystemSystemConfig     fileSystem{};
        render::RendererSystemConfig   renderer{};
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
        render::RendererSystemState renderer{};
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
                                                           const CoreRuntimeConfig& config = {}) noexcept
    {
        if (state.isInitialized)
        {
            return CoreRuntimeStatus::AlreadyInitialized;
        }

        state = CoreRuntimeState{};

        state.ownsMemorySystem = !memory::MemorySystem::IsInitialized();
        memory::MemorySystem::Init(config.memory);
        state.stage = CoreRuntimeInitStage::Memory;

        if (!time::InitTimeSystem(state.time, config.time))
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::TimeInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Time;

        if (!jobs::InitJobsSystem(state.jobs, config.jobs))
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::JobsInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Jobs;

        if (!input::InitInputSystem(state.input, config.input))
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::InputInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Input;

        if (!win::InitWindowSystem(state.window, config.window))
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::WindowInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Window;

        if (!fs::InitFileSystemSystem(state.fileSystem, config.fileSystem))
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::FileSystemInitFailed;
        }
        state.stage = CoreRuntimeInitStage::FileSystem;

        if (!render::InitRendererSystem(state.renderer, config.renderer))
        {
            ShutdownCoreRuntime(state);
            return CoreRuntimeStatus::RendererInitFailed;
        }
        state.stage = CoreRuntimeInitStage::Renderer;

        state.isInitialized = true;
        state.stage = CoreRuntimeInitStage::Ready;
        return CoreRuntimeStatus::Ok;
    }

} // namespace dng::runtime
