#include "Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp"

namespace
{
    void ExerciseBasicForwardRenderer() noexcept
    {
        dng::render::BasicForwardRenderer backend{};
        auto iface = dng::render::MakeBasicForwardRendererInterface(backend);

        dng::render::FrameSubmission submission{};
        dng::render::BeginFrame(iface, submission);
        dng::render::SubmitInstances(iface, nullptr, 0);
        dng::render::EndFrame(iface);
        (void)dng::render::QueryCaps(iface);
    }

    static_assert(dng::render::RendererBackend<dng::render::BasicForwardRenderer>,
                  "BasicForwardRenderer must satisfy renderer backend concept.");
}
