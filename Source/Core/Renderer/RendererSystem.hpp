// ============================================================================
// D-Engine - Source/Core/Renderer/RendererSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level renderer system that owns a backend instance and exposes
//           a unified entry point (RenderFrame) to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to RendererSystemState.
//           Thread-safety and determinism follow RendererCaps from the backend;
//           callers must serialize access per instance.
// Notes   : For now only the NullRenderer backend is owned directly. External
//           backends (forward, RT, GPU-driven) can plug in via a renderer interface.

// ============================================================================

#pragma once

#include "Core/Contracts/Renderer.hpp"
#include "Core/Renderer/NullRenderer.hpp"

namespace dng::render
{
    enum class RendererSystemBackend : dng::u8
    {
        Null,
        Forward
        // Future options (VisibilityBuffer, RT, etc.) will be appended here.
    };

    struct RendererSystemConfig
    {
        RendererSystemBackend backend = RendererSystemBackend::Null;
        // Later: window handles, debug flags, vsync, etc.
    };

    struct RendererSystemState
    {
        RendererInterface     interface{};
        RendererSystemBackend backend = RendererSystemBackend::Null;
        NullRenderer          nullBackend{}; // Owned only when backend==Null.
        bool                  isInitialized = false;
    };

    // Purpose : Initialize the system from a caller-provided renderer interface.
    // Contract: Does not allocate or throw. Caller retains ownership of the
    //           backend object referenced by `interface`. State must not outlive it.
    // Notes   : Preferred entry point for modules to plug into Core without
    //           adding new dependencies.
    [[nodiscard]] inline bool InitRendererSystemWithInterface(RendererSystemState& state,
                                                             RendererInterface interface,
                                                             RendererSystemBackend backend) noexcept
    {
        state              = RendererSystemState{};
        if (interface.userData == nullptr ||
            interface.vtable.getCaps == nullptr ||
            interface.vtable.beginFrame == nullptr ||
            interface.vtable.submitInstances == nullptr ||
            interface.vtable.endFrame == nullptr ||
            interface.vtable.resizeSurface == nullptr)
        {
            return false;
        }

        state.interface    = interface;
        state.backend      = backend;
        state.isInitialized = true;
        return true;

    }

    // Purpose : Initialize the renderer system with the requested backend.
    // Contract: Must be called before RenderFrame. Returns false only if the
    //           backend cannot be created. No allocations, no throws.
    // Notes   : Delegates to the generic initializer even for Null backends.
    [[nodiscard]] inline bool InitRendererSystem(RendererSystemState& state,
                                                 const RendererSystemConfig& config) noexcept
    {
        switch (config.backend)
        {
            case RendererSystemBackend::Null:
            default:
            {
                RendererInterface iface = MakeNullRendererInterface(state.nullBackend);
                return InitRendererSystemWithInterface(state, iface, RendererSystemBackend::Null);
            }
            case RendererSystemBackend::Forward:
            {
                // Core does not own forward backends; caller must inject via InitRendererSystemWithInterface.
                return false;
            }
        }

        return false;
    }

    // Purpose : Tear down the renderer system and reset state to defaults.
    // Contract: Safe to call even if the system was never initialized. No
    //           allocations or logging; backend storage stays inline.
    // Notes   : Future backends can release API-specific resources here.
    inline void ShutdownRendererSystem(RendererSystemState& state) noexcept
    {
        state.interface     = RendererInterface{};
        state.backend       = RendererSystemBackend::Null;
        state.nullBackend   = NullRenderer{};
        state.isInitialized = false;
    }

    // Purpose : Drive the active backend for a single frame.
    // Contract: InitRendererSystem must have succeeded earlier. Submission
    //           views/instances must remain valid for the duration of the call.
    // Notes   : No allocations or synchronization; higher layers own ordering.
    [[nodiscard]] inline RendererCaps QueryCaps(const RendererSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : RendererCaps{};
    }

    inline void RenderFrame(RendererSystemState& state,
                            const FrameSubmission& submission) noexcept
    {
        if (!state.isInitialized)
        {
            return;
        }

        BeginFrame(state.interface, submission);
        SubmitInstances(state.interface, submission.instances, submission.instanceCount);
        EndFrame(state.interface);
    }


} // namespace dng::render
