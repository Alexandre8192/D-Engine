#include "Core/Renderer/RendererSystem.hpp"
#include "Core/Contracts/Renderer.hpp"

int RunRendererSystemSmoke()
{
    using namespace dng::render;

    RendererSystemConfig config{};
    config.backend = RendererSystemBackend::Null;

    RendererSystemState state{};
    if (!InitRendererSystem(state, config))
    {
        return 1;
    }

    const RendererCaps caps = QueryCaps(state.interface);
    if (caps.determinism != dng::DeterminismMode::Replay ||
        caps.threadSafety != dng::ThreadSafetyMode::ExternalSync ||
        !caps.stableSubmissionRequired)
    {
        return 2;
    }

    FrameSubmission submission{};
    RenderFrame(state, submission);


    ShutdownRendererSystem(state);
    return 0;
}
