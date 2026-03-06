#include "Core/Renderer/RendererSystem.hpp"
#include "Core/Contracts/Renderer.hpp"

int RunRendererSystemSmoke()
{
    using namespace dng::render;

    const auto isReset = [](const RendererSystemState& state) noexcept
    {
        const RendererCaps caps = QueryCaps(state);
        return !state.isInitialized &&
               state.backend == RendererSystemBackend::Null &&
               caps.determinism == dng::DeterminismMode::Unknown &&
               caps.threadSafety == dng::ThreadSafetyMode::Unknown &&
               !caps.stableSubmissionRequired;
    };

    RendererSystemState uninitialized{};
    if (!isReset(uninitialized))
    {
        return 3;
    }

    RendererSystemConfig config{};
    config.backend = RendererSystemBackend::Null;

    NullRenderer nullBackendForValidation{};
    RendererInterface brokenInterface = MakeNullRendererInterface(nullBackendForValidation);
    brokenInterface.vtable.getCaps = nullptr;
    RendererSystemState rejected{};
    if (!InitRendererSystem(rejected, config))
    {
        return 4;
    }
    if (InitRendererSystemWithInterface(rejected, brokenInterface))
    {
        return 5;
    }
    if (!isReset(rejected))
    {
        return 6;
    }

    RendererSystemConfig rejectedConfig{};
    rejectedConfig.backend = RendererSystemBackend::External;
    if (!InitRendererSystem(rejected, config))
    {
        return 7;
    }
    if (InitRendererSystem(rejected, rejectedConfig))
    {
        return 8;
    }
    if (!isReset(rejected))
    {
        return 9;
    }

    RendererSystemState state{};
    if (!InitRendererSystem(state, config))
    {
        return 1;
    }

    const RendererCaps caps = QueryCaps(state);
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
