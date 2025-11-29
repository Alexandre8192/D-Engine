#include "Core/Renderer/NullRenderer.hpp"

namespace
{
    void ExerciseNullRenderer() noexcept
    {
        dng::render::NullRenderer backend{};
        auto iface = dng::render::MakeNullRendererInterface(backend);

        const dng::render::RendererCaps caps = dng::render::QueryCaps(iface);
        (void)caps;

        dng::render::FrameSubmission frame{};
        dng::render::BeginFrame(iface, frame);
        dng::render::SubmitInstances(iface, nullptr, 0);
        dng::render::EndFrame(iface);
        dng::render::ResizeSurface(iface, 1280U, 720U);
    }
}
