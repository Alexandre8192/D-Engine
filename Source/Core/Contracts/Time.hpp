// ============================================================================
// D-Engine - Source/Core/Contracts/Time.hpp
// ----------------------------------------------------------------------------
// Purpose : Time contract describing backend-agnostic clocks and frame timing
//           data so multiple time providers can plug into Core without leaking
//           platform details.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is left to the backend; callers must
//           externally synchronize per backend instance.
// Notes   : The contract models nanosecond timestamps and minimal capability
//           flags. Backends may be synthetic (NullTime) or platform-backed.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::time
{
    using Nanoseconds = dng::u64;

    // ------------------------------------------------------------------------
    // Backend metadata and capabilities
    // ------------------------------------------------------------------------

    struct TimeCaps
    {
        bool monotonic = false;
        bool high_res  = false;
        dng::DeterminismMode determinism = dng::DeterminismMode::Unknown;
        dng::ThreadSafetyMode threadSafety = dng::ThreadSafetyMode::Unknown;
        bool stableSampleOrder = false;
    };

    static_assert(std::is_trivially_copyable_v<TimeCaps>, "TimeCaps must stay POD for telemetry dumps.");

    // ------------------------------------------------------------------------
    // Frame timing data
    // ------------------------------------------------------------------------

    struct FrameTime
    {
        dng::u64   frameIndex = 0;
        Nanoseconds deltaNs   = 0;
        Nanoseconds totalNs   = 0;
    };

    static_assert(std::is_trivially_copyable_v<FrameTime>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    struct TimeVTable
    {
        using GetCapsFunc        = TimeCaps(*)(const void* userData) noexcept;
        using NowMonotonicFunc   = Nanoseconds(*)(void* userData) noexcept;
        using BeginFrameFunc     = void(*)(void* userData) noexcept;
        using EndFrameFunc       = void(*)(void* userData) noexcept;

        GetCapsFunc      getCaps      = nullptr;
        NowMonotonicFunc nowMonotonic = nullptr;
        BeginFrameFunc   beginFrame   = nullptr;
        EndFrameFunc     endFrame     = nullptr;
    };

    struct TimeInterface
    {
        TimeVTable vtable{};
        void*      userData = nullptr; // Non-owning backend instance pointer.
    };

    [[nodiscard]] inline TimeCaps QueryCaps(const TimeInterface& time) noexcept
    {
        return (time.vtable.getCaps && time.userData)
            ? time.vtable.getCaps(time.userData)
            : TimeCaps{};
    }

    inline void BeginFrame(TimeInterface& time) noexcept
    {
        if (time.vtable.beginFrame && time.userData)
        {
            time.vtable.beginFrame(time.userData);
        }
    }

    inline void EndFrame(TimeInterface& time) noexcept
    {
        if (time.vtable.endFrame && time.userData)
        {
            time.vtable.endFrame(time.userData);
        }
    }

    [[nodiscard]] inline Nanoseconds NowMonotonicNs(TimeInterface& time) noexcept
    {
        return (time.vtable.nowMonotonic && time.userData)
            ? time.vtable.nowMonotonic(time.userData)
            : Nanoseconds{0};
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    template <typename Backend>
    concept TimeBackend = requires(Backend& backend, const Backend& constBackend)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<TimeCaps>;
        { backend.NowMonotonicNs() } noexcept -> std::same_as<Nanoseconds>;
        { backend.BeginFrame() } noexcept -> std::same_as<void>;
        { backend.EndFrame() } noexcept -> std::same_as<void>;
    };

    namespace detail
    {
        template <typename Backend>
        struct TimeInterfaceAdapter
        {
            static TimeCaps GetCaps(const void* userData) noexcept
            {
                return static_cast<const Backend*>(userData)->GetCaps();
            }

            static Nanoseconds NowMonotonic(void* userData) noexcept
            {
                return static_cast<Backend*>(userData)->NowMonotonicNs();
            }

            static void BeginFrame(void* userData) noexcept
            {
                static_cast<Backend*>(userData)->BeginFrame();
            }

            static void EndFrame(void* userData) noexcept
            {
                static_cast<Backend*>(userData)->EndFrame();
            }
        };
    } // namespace detail

    template <typename Backend>
    [[nodiscard]] inline TimeInterface MakeTimeInterface(Backend& backend) noexcept
    {
        static_assert(TimeBackend<Backend>, "Backend must satisfy TimeBackend concept.");

        TimeInterface iface{};
        iface.userData           = &backend;
        iface.vtable.getCaps     = &detail::TimeInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.nowMonotonic = &detail::TimeInterfaceAdapter<Backend>::NowMonotonic;
        iface.vtable.beginFrame  = &detail::TimeInterfaceAdapter<Backend>::BeginFrame;
        iface.vtable.endFrame    = &detail::TimeInterfaceAdapter<Backend>::EndFrame;
        return iface;
    }

} // namespace dng::time
