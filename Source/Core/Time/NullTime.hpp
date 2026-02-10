// ============================================================================
// D-Engine - Source/Core/Time/NullTime.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal time backend that satisfies the time contract without
//           relying on platform clocks. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations. All methods are
//           noexcept and deterministic.
// Notes   : Advances an internal monotonic counter by a fixed step each call
//           to NowMonotonicNs so tests can observe progress without wall time.
// ============================================================================

#pragma once

#include "Core/Contracts/Time.hpp"

namespace dng::time
{
    struct NullTime
    {
        Nanoseconds currentNs = 0;
        Nanoseconds stepNs    = 16'000'000; // ~16 ms step for deterministic progression.

        [[nodiscard]] constexpr TimeCaps GetCaps() const noexcept
        {
            TimeCaps caps{};
            caps.monotonic = true;
            caps.high_res  = false;
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableSampleOrder = true;
            return caps;
        }

        [[nodiscard]] Nanoseconds NowMonotonicNs() noexcept
        {
            currentNs += stepNs;
            return currentNs;
        }

        void BeginFrame() noexcept {}
        void EndFrame() noexcept {}
    };

    static_assert(TimeBackend<NullTime>, "NullTime must satisfy time backend concept.");

    [[nodiscard]] inline TimeInterface MakeNullTimeInterface(NullTime& backend) noexcept
    {
        return MakeTimeInterface(backend);
    }

} // namespace dng::time
