// ============================================================================
// D-Engine - Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp
// ----------------------------------------------------------------------------
// Purpose : Skeleton forward renderer backend implementing the RendererBackend
//           contract without talking to any GPU yet. Serves as a starting point
//           for a simple educational forward renderer.
// Contract: Header-first, no exceptions/RTTI, no hidden allocations. All
//           public methods are noexcept and follow the Core renderer contract.
// Notes   : This backend is currently a stub; it records basic state such as
//           surface size and instance counts but performs no real rendering.
// ============================================================================

#pragma once

#include "Core/Contracts/Renderer.hpp"

namespace dng::render
{
    // Purpose : Simple telemetry for the BasicForwardRenderer backend.
    // Contract: Trivially copyable, no allocations. Reflects the last frame
    //           observed by this backend.
    // Notes   : Intended for debugging, tests, and educational usage.
    struct BasicForwardRendererStats
    {
        dng::u32 frameIndex        = 0;
        dng::u32 lastViewCount     = 0;
        dng::u32 lastInstanceCount = 0;
        dng::u32 surfaceWidth      = 0;
        dng::u32 surfaceHeight     = 0;
    };

    struct BasicForwardRenderer
    {
        BasicForwardRendererStats stats{};

        // Purpose : Report capability flags for the forward renderer backend.
        // Contract: Allocation-free, noexcept, returns the same caps every call
        //           until the implementation evolves.
        // Notes   : All advanced features remain disabled for now.
        [[nodiscard]] constexpr RendererCaps GetCaps() const noexcept
        {
            return RendererCaps{};
        }

        // Purpose : Cache per-frame submission metadata (views, surface size).
        // Contract: Must not allocate or throw. Safe to call with zero views.
        // Notes   : Only caches the first view when present.
        void BeginFrame(const FrameSubmission& submission) noexcept
        {
            stats.frameIndex += 1;
            stats.lastViewCount = submission.viewCount;
            if (submission.viewCount > 0U && submission.views != nullptr)
            {
                stats.surfaceWidth  = submission.views[0].width;
                stats.surfaceHeight = submission.views[0].height;
            }

            stats.lastInstanceCount = 0;
        }

        // Purpose : Receive render instances and record basic statistics.
        // Contract: Must not allocate or throw. Safe to pass nullptr/count 0.
        // Notes   : Currently only stores the last instance count.
        void SubmitInstances(const RenderInstance*, dng::u32 count) noexcept
        {
            stats.lastInstanceCount += count;
        }

        // Purpose : Mark the end of a frame.
        // Contract: No allocations or logging. May update internal statistics.
        // Notes   : Intentionally lightweight until a real pipeline is added.
        void EndFrame() noexcept {}

        // Purpose : Update cached surface size, usually forwarded from the window.
        // Contract: Stores width/height verbatim; must be noexcept and allocation-free.
        // Notes   : Mirrors the NullRenderer behavior so tests can rely on it.
        void ResizeSurface(dng::u32 newWidth, dng::u32 newHeight) noexcept
        {
            stats.surfaceWidth  = newWidth;
            stats.surfaceHeight = newHeight;
        }

        // Purpose : Expose the latest stats snapshot for this backend.
        // Contract: No allocations, no locking. Safe to call at any time.
        // Notes   : Stats are updated by frame events and reflect the last frame.
        [[nodiscard]] constexpr const BasicForwardRendererStats& GetStats() const noexcept
        {
            return stats;
        }
    };

    static_assert(RendererBackend<BasicForwardRenderer>,
                  "BasicForwardRenderer must satisfy renderer backend concept.");

    // Purpose : Helper to expose BasicForwardRenderer through the dynamic interface.
    // Contract: Does not transfer ownership; caller must keep backend alive.
    // Notes   : Wraps MakeRendererInterface with RendererBackendKind::Forward.
    [[nodiscard]] inline RendererInterface MakeBasicForwardRendererInterface(BasicForwardRenderer& backend) noexcept
    {
        return MakeRendererInterface(backend, RendererBackendKind::Forward);
    }

} // namespace dng::render
