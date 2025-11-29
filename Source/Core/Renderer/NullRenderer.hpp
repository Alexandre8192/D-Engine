// ============================================================================
// D-Engine - Source/Core/Renderer/NullRenderer.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal renderer backend that satisfies the renderer contract
//           without talking to any GPU. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations, engine-absolute
//           includes only. All methods are noexcept, deterministic, and ignore
//           submitted data. Thread-safety: callers must provide external
//           synchronization; NullRenderer stores only primitive POD state.
// Notes   : Serves as a pedagogical reference for future renderer backends.
// ============================================================================

#pragma once

#include "Core/Contracts/Renderer.hpp"

namespace dng::render
{
    // Purpose : Trivial backend that fulfills RendererBackend without doing work.
    // Contract: Never allocates, never logs, never throws, and ignores all
    //           submissions. Stores viewport size locally for introspection.
    // Notes   : Acts as a drop-in backend for headless or CI builds.
    struct NullRenderer
    {
        dng::u32 width  = 0;
        dng::u32 height = 0;

        // Purpose : Reports conservative capability flags (everything disabled).
        // Contract: Allocation-free, noexcept, returns the same caps every call.
        // Notes   : Can be extended to expose fake features for testing.
        [[nodiscard]] constexpr RendererCaps GetCaps() const noexcept
        {
            return RendererCaps{};
        }

        // Purpose : Receives per-frame submission metadata and caches viewport.
        // Contract: Does not allocate or log; ignores submission contents.
        // Notes   : width/height are inferred from the first view when present.
        void BeginFrame(const FrameSubmission& submission) noexcept
        {
            if (submission.viewCount > 0U && submission.views != nullptr)
            {
                width  = submission.views[0].width;
                height = submission.views[0].height;
            }
        }

        // Purpose : Ignores all render instances while satisfying the contract.
        // Contract: Never allocates or logs; safe to pass nullptr/count 0.
        // Notes   : Provided to keep API symmetry with real backends.
        void SubmitInstances(const RenderInstance*, dng::u32) noexcept {}

        // Purpose : Marks the end of a frame for symmetry with BeginFrame.
        // Contract: No allocations, logging, or state beyond width/height.
        // Notes   : Intentionally empty.
        void EndFrame() noexcept {}

        // Purpose : Updates cached surface size to mirror swapchain changes.
        // Contract: Width/height are stored verbatim; no allocations/logging.
        // Notes   : Allows tests to verify resize propagation without rendering.
        void ResizeSurface(dng::u32 newWidth, dng::u32 newHeight) noexcept
        {
            width  = newWidth;
            height = newHeight;
        }
    };

    static_assert(RendererBackend<NullRenderer>, "NullRenderer must satisfy renderer backend concept.");

    // Purpose : Helper to expose NullRenderer through the dynamic interface.
    // Contract: Does not transfer ownership; caller must keep backend alive.
    // Notes   : Wraps MakeRendererInterface with RendererBackendKind::Null.
    [[nodiscard]] inline RendererInterface MakeNullRendererInterface(NullRenderer& backend) noexcept
    {
        return MakeRendererInterface(backend, RendererBackendKind::Null);
    }

} // namespace dng::render
