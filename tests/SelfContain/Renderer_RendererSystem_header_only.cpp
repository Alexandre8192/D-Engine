#include "Core/Renderer/RendererSystem.hpp"

namespace
{
    void ExerciseRendererSystem() noexcept
    {
        dng::render::RendererSystemState state{};
        dng::render::RendererSystemConfig config{};
        config.backend = dng::render::RendererSystemBackend::Null;
        (void)dng::render::InitRendererSystem(state, config);

        dng::render::FrameSubmission submission{};
        dng::render::RenderFrame(state, submission);

        dng::render::ShutdownRendererSystem(state);
    }
}
