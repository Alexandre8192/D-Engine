#include "Core/Renderer/RendererSystem.hpp"
#include "Core/Contracts/Renderer.hpp"

int RunRendererSystemSmoke()
{
    using namespace dng::render;

    RendererSystemState uninitialized{};
    const RendererCaps uninitCaps = QueryCaps(uninitialized);
    if (uninitCaps.determinism != dng::DeterminismMode::Unknown ||
        uninitCaps.threadSafety != dng::ThreadSafetyMode::Unknown ||
        uninitCaps.stableSubmissionRequired)
    {
        return 3;
    }

    NullRenderer nullBackendForValidation{};
    RendererInterface brokenInterface = MakeNullRendererInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    RendererSystemState rejected{};
    if (InitRendererSystemWithInterface(rejected, brokenInterface, RendererSystemBackend::Forward))
    {
        return 4;
    }

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
