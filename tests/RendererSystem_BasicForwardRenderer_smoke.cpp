#include "Core/Renderer/RendererSystem.hpp"
#include "Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp"
#include "Core/Contracts/Renderer.hpp"

int RunRendererSystemBasicForwardRendererSmoke()
{
    using namespace dng::render;

    BasicForwardRenderer backend{};
    RendererInterface iface = MakeBasicForwardRendererInterface(backend);

    RendererSystemState state{};
    if (!InitRendererSystemWithInterface(state, iface, RendererSystemBackend::Forward))
    {
        return 1;
    }

    FrameSubmission submission{};
    RenderFrame(state, submission);

    const auto& stats = backend.GetStats();
    if (stats.frameIndex == 0U)
    {
        return 1;
    }

    if (stats.lastViewCount != submission.viewCount)
    {
        return 1;
    }

    if (stats.lastInstanceCount != submission.instanceCount)
    {
        return 1;
    }

    if (stats.surfaceWidth != 0U || stats.surfaceHeight != 0U)
    {
        return 1;
    }

    ShutdownRendererSystem(state);
    return 0;
}
