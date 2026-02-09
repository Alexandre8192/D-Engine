// ============================================================================
// D-Engine - Source/Core/Input/NullInput.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal input backend that satisfies the input contract without
//           producing any events. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations. All methods are
//           noexcept and deterministic.
// Notes   : Always returns Ok with zero events.
// ============================================================================

#pragma once

#include "Core/Contracts/Input.hpp"

namespace dng::input
{
    struct NullInput
    {
        [[nodiscard]] constexpr InputCaps GetCaps() const noexcept
        {
            InputCaps caps{};
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableEventOrder = true;
            return caps;
        }

        [[nodiscard]] InputStatus PollEvents(InputEvent*, dng::u32, dng::u32& outCount) noexcept
        {
            outCount = 0;
            return InputStatus::Ok;
        }
    };

    static_assert(InputBackend<NullInput>, "NullInput must satisfy input backend concept.");

    [[nodiscard]] inline InputInterface MakeNullInputInterface(NullInput& backend) noexcept
    {
        return MakeInputInterface(backend);
    }

} // namespace dng::input
