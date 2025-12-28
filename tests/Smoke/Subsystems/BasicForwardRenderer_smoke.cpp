#include "Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp"
#include "Core/Contracts/Renderer.hpp"

int RunBasicForwardRendererSmoke()
{
    using namespace dng::render;

    BasicForwardRenderer backend{};
    const auto& initialStats = backend.GetStats();
    if (initialStats.frameIndex != 0 || initialStats.lastViewCount != 0 ||
        initialStats.lastInstanceCount != 0 || initialStats.surfaceWidth != 0 ||
        initialStats.surfaceHeight != 0)
    {
        return 1;
    }

    backend.ResizeSurface(1280U, 720U);
    if (backend.GetStats().surfaceWidth != 1280U || backend.GetStats().surfaceHeight != 720U)
    {
        return 1;
    }

    FrameSubmission submission{};
    RenderView views[1]{};
    views[0].width  = 800U;
    views[0].height = 600U;
    submission.views     = views;
    submission.viewCount = 1;

    auto iface = MakeBasicForwardRendererInterface(backend);
    BeginFrame(iface, submission);
    SubmitInstances(iface, nullptr, 3U);
    EndFrame(iface);

    const auto& stats = backend.GetStats();
    if (stats.frameIndex != 1U || stats.lastViewCount != 1U ||
        stats.lastInstanceCount != 3U || stats.surfaceWidth != 800U ||
        stats.surfaceHeight != 600U)
    {
        return 1;
    }

    (void)backend.GetCaps();

    return 0;
}
