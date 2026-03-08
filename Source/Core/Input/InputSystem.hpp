// ============================================================================
// D-Engine - Source/Core/Input/InputSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level input system that owns a backend instance and exposes
//           poll-based event retrieval to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to InputSystemState.
//           Thread-safety and determinism follow InputCaps from the backend;
//           callers must serialize access per instance.
// Notes   : Defaults to the NullInput backend but accepts external backends
//           via interface injection.
// ============================================================================

#pragma once

#include "Core/Contracts/Input.hpp"
#include "Core/Input/NullInput.hpp"

namespace dng::input
{
    enum class InputSystemBackend : dng::u8
    {
        Null,
        External
    };

    struct InputSystemConfig
    {
        InputSystemBackend backend = InputSystemBackend::Null;
    };

    struct InputSystemState
    {
        InputInterface     interface{};
        InputSystemBackend backend       = InputSystemBackend::Null;
        NullInput          nullBackend{};
        bool               isInitialized = false;
    };

    namespace detail
    {
        [[nodiscard]] inline bool IsValidInputSystemInterface(const InputInterface& interface) noexcept
        {
            return interface.userData != nullptr &&
                   interface.vtable.getCaps != nullptr &&
                   interface.vtable.pollEvents != nullptr;
        }

        inline void ResetInputSystemState(InputSystemState& state) noexcept
        {
            state = InputSystemState{};
        }

        [[nodiscard]] inline bool BindInputSystemState(InputSystemState& state,
                                                       InputInterface interface,
                                                       InputSystemBackend backend) noexcept
        {
            if (!IsValidInputSystemInterface(interface))
            {
                return false;
            }

            state.interface     = interface;
            state.backend       = backend;
            state.isInitialized = true;
            return true;
        }
    } // namespace detail

    [[nodiscard]] inline bool InitInputSystemWithInterface(InputSystemState& state,
                                                           InputInterface interface) noexcept
    {
        detail::ResetInputSystemState(state);
        return detail::BindInputSystemState(state, interface, InputSystemBackend::External);
    }

    [[nodiscard]] inline bool InitInputSystem(InputSystemState& state,
                                              const InputSystemConfig& config) noexcept
    {
        detail::ResetInputSystemState(state);

        switch (config.backend)
        {
            case InputSystemBackend::Null:
            {
                InputInterface iface = MakeNullInputInterface(state.nullBackend);
                return detail::BindInputSystemState(state, iface, InputSystemBackend::Null);
            }
            case InputSystemBackend::External:
            default:
            {
                return false;
            }
        }
    }

    inline void ShutdownInputSystem(InputSystemState& state) noexcept
    {
        detail::ResetInputSystemState(state);
    }

    [[nodiscard]] inline InputCaps QueryCaps(const InputSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : InputCaps{};
    }

    [[nodiscard]] inline InputStatus PollEvents(InputSystemState& state, InputEvent* events, dng::u32 capacity, dng::u32& outCount) noexcept
    {
        if (!state.isInitialized)
        {
            outCount = 0;
            return InputStatus::InvalidArg;
        }
        return PollEvents(state.interface, events, capacity, outCount);
    }

} // namespace dng::input
