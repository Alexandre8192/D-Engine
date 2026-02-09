// ============================================================================
// D-Engine - Source/Core/Window/NullWindow.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal window backend that satisfies the window contract without
//           talking to any platform APIs. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations. All methods are
//           noexcept and deterministic.
// Notes   : Simulates a single dummy window handle (value=1) and reports the
//           stored surface size. PollEvents yields zero events.
// ============================================================================

#pragma once

#include "Core/Contracts/Window.hpp"

namespace dng::win
{
    struct NullWindow
    {
        WindowHandle handle{1};
        dng::u32     width  = 0;
        dng::u32     height = 0;

        [[nodiscard]] constexpr WindowCaps GetCaps() const noexcept
        {
            WindowCaps caps{};
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableEventOrder = true;
            return caps;
        }

        [[nodiscard]] WindowStatus CreateWindow(const WindowDesc& desc, WindowHandle& outHandle) noexcept
        {
            width     = desc.width;
            height    = desc.height;
            outHandle = handle;
            return WindowStatus::Ok;
        }

        [[nodiscard]] WindowStatus DestroyWindow(WindowHandle h) noexcept
        {
            if (h.value != handle.value)
            {
                return WindowStatus::InvalidArg;
            }
            return WindowStatus::Ok;
        }

        [[nodiscard]] WindowStatus PollEvents(WindowEvent*, dng::u32, dng::u32& outCount) noexcept
        {
            outCount = 0;
            return WindowStatus::Ok;
        }

        [[nodiscard]] WindowStatus GetSurfaceSize(WindowHandle h, dng::u32& outWidth, dng::u32& outHeight) noexcept
        {
            if (h.value != handle.value)
            {
                outWidth  = 0;
                outHeight = 0;
                return WindowStatus::InvalidArg;
            }

            outWidth  = width;
            outHeight = height;
            return WindowStatus::Ok;
        }
    };

    static_assert(WindowBackend<NullWindow>, "NullWindow must satisfy window backend concept.");

    [[nodiscard]] inline WindowInterface MakeNullWindowInterface(NullWindow& backend) noexcept
    {
        return MakeWindowInterface(backend);
    }

} // namespace dng::win
