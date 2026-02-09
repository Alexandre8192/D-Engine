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

    [[nodiscard]] inline bool InitInputSystemWithInterface(InputSystemState& state,
                                                           InputInterface interface,
                                                           InputSystemBackend backend) noexcept
    {
        if (interface.userData == nullptr ||
            interface.vtable.getCaps == nullptr ||
            interface.vtable.pollEvents == nullptr)
        {
            return false;
        }

        state.interface     = interface;
        state.backend       = backend;
        state.isInitialized = true;
        return true;
    }

    [[nodiscard]] inline bool InitInputSystem(InputSystemState& state,
                                              const InputSystemConfig& config) noexcept
    {
        state = InputSystemState{};

        switch (config.backend)
        {
            case InputSystemBackend::Null:
            {
                InputInterface iface = MakeNullInputInterface(state.nullBackend);
                return InitInputSystemWithInterface(state, iface, InputSystemBackend::Null);
            }
            case InputSystemBackend::External:
            {
                return false; // Must be injected via InitInputSystemWithInterface.
            }
            default:
            {
                return false;
            }
        }
    }

    inline void ShutdownInputSystem(InputSystemState& state) noexcept
    {
        state.interface     = InputInterface{};
        state.backend       = InputSystemBackend::Null;
        state.nullBackend   = NullInput{};
        state.isInitialized = false;
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
