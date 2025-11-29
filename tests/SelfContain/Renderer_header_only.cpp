#include "Core/Contracts/Renderer.hpp"

namespace
{
    struct DummyRenderer
    {
        [[nodiscard]] constexpr dng::render::RendererCaps GetCaps() const noexcept { return {}; }
        void BeginFrame(const dng::render::FrameSubmission&) noexcept {}
        void SubmitInstances(const dng::render::RenderInstance*, dng::u32) noexcept {}
        void EndFrame() noexcept {}
        void ResizeSurface(dng::u32, dng::u32) noexcept {}
    };

    static_assert(dng::render::RendererBackend<DummyRenderer>, "DummyRenderer must satisfy the renderer contract.");

    void UseRendererInterface() noexcept
    {
        DummyRenderer backend{};
        auto iface = dng::render::MakeRendererInterface(backend, dng::render::RendererBackendKind::Null);
        dng::render::FrameSubmission submission{};
        dng::render::BeginFrame(iface, submission);
        dng::render::SubmitInstances(iface, nullptr, 0);
        dng::render::EndFrame(iface);
        (void)dng::render::QueryCaps(iface);
    }
}
