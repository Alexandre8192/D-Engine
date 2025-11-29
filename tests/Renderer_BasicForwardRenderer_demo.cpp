// ============================================================================
// D-Engine - Tests/Renderer_BasicForwardRenderer_demo.cpp
// ----------------------------------------------------------------------------
// Purpose : Minimal end-to-end example wiring BasicForwardRenderer through
//           RendererSystem and driving a few frames while inspecting stats.
// Contract: No exceptions/RTTI, no dynamic allocations, ASCII-only comments.
// Notes   : This is an educational demo, not a benchmark or real renderer.
// ============================================================================

#include "Core/Contracts/Renderer.hpp"
#include "Core/Renderer/RendererSystem.hpp"
#include "Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp"

int main()
{
    using namespace dng::render;

    BasicForwardRenderer backend{};
    RendererInterface iface = MakeBasicForwardRendererInterface(backend);

    RendererSystemState systemState{};
    if (!InitRendererSystemWithInterface(systemState, iface, RendererSystemBackend::Forward))
    {
        return 1;
    }

    RenderView view{};
    view.width  = 1280U;
    view.height = 720U;
    RenderView views[1]{ view };

    RenderInstance instances[3]{};

    constexpr dng::u32 kFrameCount = 3U;
    for (dng::u32 frame = 0U; frame < kFrameCount; ++frame)
    {
        FrameSubmission submission{};
        submission.views         = views;
        submission.viewCount     = 1U;
        submission.instances     = instances;
        submission.instanceCount = 3U;
        submission.frameIndex    = frame;
        submission.deltaTimeSec  = 1.0F / 60.0F;

        RenderFrame(systemState, submission);
    }

    const BasicForwardRendererStats& stats = backend.GetStats();
    if (stats.frameIndex != kFrameCount)
    {
        return 1;
    }
    if (stats.lastViewCount != 1U)
    {
        return 1;
    }
    if (stats.lastInstanceCount != 3U)
    {
        return 1;
    }
    if (stats.surfaceWidth != 1280U || stats.surfaceHeight != 720U)
    {
        return 1;
    }

    ShutdownRendererSystem(systemState);
    return 0;
}
