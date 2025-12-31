#include "Modules/Renderer/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp"
#include "Core/Contracts/Renderer.hpp"

#include <cstdio>

int RunBasicForwardRendererSmoke()
{
    using namespace dng::render;

    BasicForwardRenderer backend{};
    const auto& initialStats = backend.GetStats();
    if (initialStats.frameIndex != 0 || initialStats.lastViewCount != 0 ||
        initialStats.lastInstanceCount != 0 || initialStats.surfaceWidth != 0 ||
        initialStats.surfaceHeight != 0)
    {
        std::printf("BasicForwardRenderer initial stats mismatch: fi=%u, views=%u, inst=%u, w=%u, h=%u\n",
                    initialStats.frameIndex,
                    initialStats.lastViewCount,
                    initialStats.lastInstanceCount,
                    initialStats.surfaceWidth,
                    initialStats.surfaceHeight);
        return 1;
    }

    backend.ResizeSurface(1280U, 720U);
    const auto& resizedStats = backend.GetStats();
    if (resizedStats.surfaceWidth != 1280U || resizedStats.surfaceHeight != 720U)
    {
        std::printf("BasicForwardRenderer resize mismatch: w=%u, h=%u\n",
                    resizedStats.surfaceWidth,
                    resizedStats.surfaceHeight);
        return 2;
    }

    FrameSubmission submission{};
    RenderView views[1]{};
    views[0].width  = 800U;
    views[0].height = 600U;
    submission.views     = views;
    submission.viewCount = 1;

    auto iface = MakeBasicForwardRendererInterface(backend);
    BeginFrame(iface, submission);
    RenderInstance instances[3]{};
    SubmitInstances(iface, instances, 3U);
    EndFrame(iface);

    const auto& stats = backend.GetStats();
    if (stats.frameIndex != 1U || stats.lastViewCount != 1U ||
        stats.lastInstanceCount != 3U || stats.surfaceWidth != 800U ||
        stats.surfaceHeight != 600U)
    {
        std::printf("BasicForwardRenderer frame stats mismatch: fi=%u, views=%u, inst=%u, w=%u, h=%u\n",
                    stats.frameIndex,
                    stats.lastViewCount,
                    stats.lastInstanceCount,
                    stats.surfaceWidth,
                    stats.surfaceHeight);
        return 3;
    }

    (void)backend.GetCaps();

    return 0;
}
