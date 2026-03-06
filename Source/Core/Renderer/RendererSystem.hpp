// ============================================================================
// D-Engine - Source/Core/Renderer/RendererSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level renderer system that owns a backend instance and exposes
//           a unified entry point (RenderFrame) to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to RendererSystemState.
//           Thread-safety and determinism follow RendererCaps from the backend;
//           callers must serialize access per instance.
// Notes   : For now only the NullRenderer backend is owned directly. All other
//           renderer implementations are injected via a renderer interface.

// ============================================================================

#pragma once

#include "Core/Contracts/Renderer.hpp"
#include "Core/Renderer/NullRenderer.hpp"

namespace dng::render
{
    enum class RendererSystemBackend : dng::u8
    {
        Null,
        External
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

    namespace detail
    {
        [[nodiscard]] inline bool IsValidRendererSystemInterface(const RendererInterface& interface) noexcept
        {
            return interface.userData != nullptr &&
                   interface.vtable.getCaps != nullptr &&
                   interface.vtable.beginFrame != nullptr &&
                   interface.vtable.submitInstances != nullptr &&
                   interface.vtable.endFrame != nullptr &&
                   interface.vtable.resizeSurface != nullptr;
        }

        inline void ResetRendererSystemState(RendererSystemState& state) noexcept
        {
            state = RendererSystemState{};
        }

        [[nodiscard]] inline bool BindRendererSystemState(RendererSystemState& state,
                                                          RendererInterface interface,
                                                          RendererSystemBackend backend) noexcept
        {
            if (!IsValidRendererSystemInterface(interface))
            {
                return false;
            }

            state.interface      = interface;
            state.backend        = backend;
            state.isInitialized  = true;
            return true;
        }
    } // namespace detail

    // Purpose : Initialize the system from a caller-provided renderer interface.
    // Contract: Does not allocate or throw. Caller retains ownership of the
    //           injected backend object referenced by `interface`. State must
    //           not outlive it.
    // Notes   : Preferred entry point for external modules to plug into Core without
    //           adding new dependencies.
    [[nodiscard]] inline bool InitRendererSystemWithInterface(RendererSystemState& state,
                                                              RendererInterface interface) noexcept
    {
        detail::ResetRendererSystemState(state);
        return detail::BindRendererSystemState(state, interface, RendererSystemBackend::External);
    }

    // Purpose : Initialize the renderer system with the requested backend.
    // Contract: Must be called before RenderFrame. Returns false only if the
    //           backend cannot be created. No allocations, no throws.
    // Notes   : Delegates to the generic initializer even for Null backends.
    [[nodiscard]] inline bool InitRendererSystem(RendererSystemState& state,
                                                 const RendererSystemConfig& config) noexcept
    {
        detail::ResetRendererSystemState(state);

        switch (config.backend)
        {
            case RendererSystemBackend::Null:
            {
                RendererInterface iface = MakeNullRendererInterface(state.nullBackend);
                return detail::BindRendererSystemState(state, iface, RendererSystemBackend::Null);
            }
            case RendererSystemBackend::External:
            default:
            {
                // Non-null renderers are injected via InitRendererSystemWithInterface.
                return false;
            }
        }
    }

    // Purpose : Tear down the renderer system and reset state to defaults.
    // Contract: Safe to call even if the system was never initialized. No
    //           allocations or logging; backend storage stays inline.
    // Notes   : Future backends can release API-specific resources here.
    inline void ShutdownRendererSystem(RendererSystemState& state) noexcept
    {
        detail::ResetRendererSystemState(state);
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
