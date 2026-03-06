// ============================================================================
// D-Engine - Source/Core/Time/TimeSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level time system that owns a backend instance and exposes a
//           unified TickTimeSystem entry point to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to TimeSystemState.
//           Thread-safety and determinism follow TimeCaps from the backend;
//           callers must serialize access per instance.
// Notes   : Defaults to the NullTime backend but accepts external backends via
//           TimeInterface injection.
// ============================================================================

#pragma once

#include "Core/Contracts/Time.hpp"
#include "Core/Time/NullTime.hpp"

namespace dng::time
{
    enum class TimeSystemBackend : dng::u8
    {
        Null,
        External
    };

    struct TimeSystemConfig
    {
        TimeSystemBackend backend = TimeSystemBackend::Null;
        Nanoseconds       nullStepNs = 16'000'000; // ~16 ms default step for NullTime.
        bool              primeOnInit = true;      // When true, seed totalNs from the backend clock at init.
    };

    struct TimeSystemState
    {
        TimeInterface      interface{};
        TimeSystemBackend  backend       = TimeSystemBackend::Null;
        NullTime           nullBackend{};
        FrameTime          lastFrameTime{};
        bool               isInitialized = false;
    };

    namespace detail
    {
        [[nodiscard]] inline bool IsValidTimeSystemInterface(const TimeInterface& interface) noexcept
        {
            return interface.userData != nullptr &&
                   interface.vtable.getCaps != nullptr &&
                   interface.vtable.nowMonotonic != nullptr &&
                   interface.vtable.beginFrame != nullptr &&
                   interface.vtable.endFrame != nullptr;
        }

        inline void ResetTimeSystemState(TimeSystemState& state) noexcept
        {
            state = TimeSystemState{};
        }

        [[nodiscard]] inline bool BindTimeSystemState(TimeSystemState& state,
                                                      TimeInterface interface,
                                                      TimeSystemBackend backend,
                                                      bool primeOnInit) noexcept
        {
            if (!IsValidTimeSystemInterface(interface))
            {
                return false;
            }

            state.interface     = interface;
            state.backend       = backend;
            state.isInitialized = true;
            state.lastFrameTime = FrameTime{};

            if (primeOnInit)
            {
                state.lastFrameTime.totalNs = NowMonotonicNs(state.interface);
            }

            return true;
        }
    } // namespace detail

    [[nodiscard]] inline bool InitTimeSystemWithInterface(TimeSystemState& state,
                                                          TimeInterface interface,
                                                          bool primeOnInit = true) noexcept
    {
        detail::ResetTimeSystemState(state);
        return detail::BindTimeSystemState(state, interface, TimeSystemBackend::External, primeOnInit);
    }

    [[nodiscard]] inline bool InitTimeSystem(TimeSystemState& state,
                                             const TimeSystemConfig& config) noexcept
    {
        detail::ResetTimeSystemState(state);

        switch (config.backend)
        {
            case TimeSystemBackend::Null:
            {
                state.nullBackend.stepNs = config.nullStepNs;
                TimeInterface iface = MakeNullTimeInterface(state.nullBackend);
                return detail::BindTimeSystemState(state, iface, TimeSystemBackend::Null, config.primeOnInit);
            }
            case TimeSystemBackend::External:
            default:
            {
                return false;
            }
        }
    }

    inline void ShutdownTimeSystem(TimeSystemState& state) noexcept
    {
        detail::ResetTimeSystemState(state);
    }

    [[nodiscard]] inline TimeCaps QueryCaps(const TimeSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : TimeCaps{};
    }

    [[nodiscard]] inline FrameTime TickTimeSystem(TimeSystemState& state) noexcept
    {
        if (!state.isInitialized)
        {
            return state.lastFrameTime;
        }

        BeginFrame(state.interface);

        const Nanoseconds nowNs = NowMonotonicNs(state.interface);
        FrameTime next{};
        next.frameIndex = state.lastFrameTime.frameIndex + 1U;
        if (nowNs >= state.lastFrameTime.totalNs)
        {
            next.deltaNs = nowNs - state.lastFrameTime.totalNs;
        }
        else
        {
            next.deltaNs = 0;
        }
        next.totalNs = nowNs;

        EndFrame(state.interface);

        state.lastFrameTime = next;
        return next;
    }

} // namespace dng::time
