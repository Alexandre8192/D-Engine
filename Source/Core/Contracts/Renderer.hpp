// ============================================================================
// D-Engine - Source/Core/Contracts/Renderer.hpp
// ----------------------------------------------------------------------------
// Purpose : Renderer contract describing backend-agnostic handles, frame data,
//           and both static/dynamic faces so multiple rendering backends can
//           plug into Core without leaking implementation details.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is left to the backend; callers must
//           externally synchronize per backend instance.
// Notes   : This contract only models data flow (views, instances, handles).
//           Concrete backends (Null, DX12, Vulkan, Metal, GPU-driven, RT) live
//           elsewhere and can opt into either the static concept or the tiny
//           dynamic v-table defined here.
// ============================================================================

#pragma once

#include "Core/Types.hpp"
#include "Core/Math/Matrix.hpp"

#include <concepts>
#include <type_traits>

namespace dng::render
{
    using HandleValue = dng::u32;

    // ------------------------------------------------------------------------
    // Backend metadata and capabilities
    // ------------------------------------------------------------------------

    // Purpose : Identifies broad renderer categories for tooling/telemetry.
    // Contract: Purely descriptive; callers must not branch on this for logic.
    // Notes   : Extendable; keep existing numeric values stable for captures.
    enum class RendererBackendKind : dng::u8
    {
        Unknown = 0,
        Null,
        Forward,
        Deferred,
        GpuDriven,
        Experimental
    };

    // Purpose : Capability hints returned by every backend.
    // Contract: Flags must be immutable after initialization; callers must
    //           still provide fallbacks when a feature is unavailable.
    // Notes   : Keep flags additive; never repurpose an existing field.
    struct RendererCaps
    {
        bool supportsMeshShaders        = false;
        bool supportsBindlessResources  = false;
        bool supportsRayTracing         = false;
        bool supportsVisibilityBuffer   = false;
        bool supportsVirtualGeometry    = false;
        bool supportsIndirectSubmission = false;
        bool supportsGpuDrivenCulling   = false;
        bool supportsSoftwareOcclusion  = false;
        dng::DeterminismMode determinism = dng::DeterminismMode::Replay;
        dng::ThreadSafetyMode threadSafety = dng::ThreadSafetyMode::ExternalSync;
        bool stableSubmissionRequired = true;
    };


    static_assert(std::is_trivially_copyable_v<RendererCaps>, "RendererCaps must stay POD for telemetry dumps.");

    // ------------------------------------------------------------------------
    // Handle types (opaque, non-owning views over backend resources)
    // ------------------------------------------------------------------------

    // Purpose : Non-owning identifier referencing backend-managed mesh data.
    // Contract: Value 0 is invalid; no ownership or lifetime extension.
    // Notes   : Backends decide how ids map to vertex/index/cluster resources.
    struct MeshHandle
    {
        HandleValue value = 0;

        constexpr MeshHandle() noexcept = default;
        explicit constexpr MeshHandle(HandleValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr MeshHandle Invalid() noexcept { return MeshHandle{}; }
    };

    // Purpose : Non-owning identifier for a backend material/shader binding.
    // Contract: Value 0 is invalid; lifetime governed by backend allocator.
    // Notes   : May reference descriptor tables, bindless indices, etc.
    struct MaterialHandle
    {
        HandleValue value = 0;

        constexpr MaterialHandle() noexcept = default;
        explicit constexpr MaterialHandle(HandleValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr MaterialHandle Invalid() noexcept { return MaterialHandle{}; }
    };

    // Purpose : Non-owning identifier for backend texture resources.
    // Contract: Value 0 is invalid; does not imply residency or layout.
    // Notes   : Front-ends may use it for overrides/debugging only.
    struct TextureHandle
    {
        HandleValue value = 0;

        constexpr TextureHandle() noexcept = default;
        explicit constexpr TextureHandle(HandleValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr TextureHandle Invalid() noexcept { return TextureHandle{}; }
    };

    // Purpose : Non-owning identifier selecting pipeline/shader variants.
    // Contract: Value 0 is invalid; backend defines pipeline lifetime/state.
    // Notes   : Works for forward, deferred, visibility buffer, etc.
    struct PipelineHandle
    {
        HandleValue value = 0;

        constexpr PipelineHandle() noexcept = default;
        explicit constexpr PipelineHandle(HandleValue raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr PipelineHandle Invalid() noexcept { return PipelineHandle{}; }
    };

    static_assert(sizeof(MeshHandle) == sizeof(HandleValue), "Handles must be compact for hot-path submission.");
    static_assert(std::is_trivially_copyable_v<MeshHandle>);
    static_assert(std::is_trivially_copyable_v<MaterialHandle>);
    static_assert(std::is_trivially_copyable_v<TextureHandle>);
    static_assert(std::is_trivially_copyable_v<PipelineHandle>);

    // ------------------------------------------------------------------------
    // Frame data views
    // ------------------------------------------------------------------------

    // Purpose : Describes one camera/view for the current frame.
    // Contract: Column-major matrices following D-Engine's Mat4f convention
    //           (vectors are treated as column vectors multiplied on the right).
    // Notes   : jitter offsets enable TAA; width/height drive viewport/scissor.
    struct RenderView
    {
        dng::Mat4f viewMatrix        = dng::Mat4f::Identity();
        dng::Mat4f projectionMatrix  = dng::Mat4f::Identity();
        float      jitterX           = 0.0f;
        float      jitterY           = 0.0f;
        float      nearPlane         = 0.1f;
        float      farPlane          = 1000.0f;
        dng::u32   width             = 0;
        dng::u32   height            = 0;
        dng::u32   viewId            = 0; // Non-owning view identifier, e.g., swapchain image index.
    };

    static_assert(std::is_trivially_copyable_v<RenderView>);

    // Purpose : Describes a renderable instance referencing opaque handles.
    // Contract: worldMatrix follows the same column-major convention.
    //           viewMask selects which views render this instance (bit per view).
    // Notes   : instanceUserData is a caller-defined index into structured data.
    struct RenderInstance
    {
        dng::Mat4f    worldMatrix         = dng::Mat4f::Identity();
        MeshHandle    mesh                {};
        MaterialHandle material           {};
        PipelineHandle pipeline           {};
        TextureHandle  overrideTexture    {}; // Optional per-instance texture override for debugging.
        dng::u32       viewMask           = 0xFFFFFFFFu;
        dng::u32       instanceUserData   = 0;
    };

    static_assert(std::is_trivially_copyable_v<RenderInstance>);

    // Purpose : Aggregates per-frame submission views and instances.
    // Contract: All pointers are non-owning; lifetimes must span BeginFrame ->
    //           EndFrame. No allocations occur inside the renderer contract.
    // Notes   : frameIndex enables determinism (useful for capture/replay).
    struct FrameSubmission
    {
        const RenderView*     views          = nullptr;
        dng::u32              viewCount      = 0;
        const RenderInstance* instances      = nullptr;
        dng::u32              instanceCount  = 0;
        dng::u64              frameIndex     = 0;
        float                 deltaTimeSec   = 0.0f;
    };

    static_assert(std::is_trivially_copyable_v<FrameSubmission>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    // Purpose : Function pointer table mirroring the static renderer concept.
    // Contract: All function pointers must be either null or point to
    //           noexcept functions. userData is owned by the caller/backend.
    // Notes   : Minimal by design to keep hot calls predictable.
    struct RendererVTable
    {
        using GetCapsFunc          = RendererCaps(*)(const void* userData) noexcept;
        using BeginFrameFunc       = void(*)(void* userData, const FrameSubmission&) noexcept;
        using SubmitInstancesFunc  = void(*)(void* userData, const RenderInstance*, dng::u32) noexcept;
        using EndFrameFunc         = void(*)(void* userData) noexcept;
        using ResizeSurfaceFunc    = void(*)(void* userData, dng::u32 width, dng::u32 height) noexcept;

        GetCapsFunc         getCaps         = nullptr;
        BeginFrameFunc      beginFrame      = nullptr;
        SubmitInstancesFunc submitInstances = nullptr;
        EndFrameFunc        endFrame        = nullptr;
        ResizeSurfaceFunc   resizeSurface   = nullptr;
    };

    // Purpose : Pairs a small v-table with a caller-owned backend instance.
    // Contract: userData must outlive the interface; contract does not manage
    //           ownership or synchronization. backendKind is informational.
    // Notes   : Enables plugin-style renderers without exposing implementation types.
    struct RendererInterface
    {
        RendererVTable       vtable{};
        void*                userData        = nullptr; // Non-owning backend instance pointer.
        RendererBackendKind  backendKind     = RendererBackendKind::Unknown;
    };

    // Purpose : Query backend capabilities through the dynamic face.
    // Contract: Returns default-initialized caps if interface lacks getCaps.
    // Notes   : Callers typically cache the result once per backend instance.
    [[nodiscard]] inline RendererCaps QueryCaps(const RendererInterface& renderer) noexcept
    {
        return (renderer.vtable.getCaps && renderer.userData)
            ? renderer.vtable.getCaps(renderer.userData)
            : RendererCaps{};
    }

    // Purpose : Forward BeginFrame to the backend if the function exists.
    // Contract: submission data must remain valid until EndFrame completes.
    // Notes   : Thread-safety depends on backend; contract does not synchronize.
    inline void BeginFrame(RendererInterface& renderer, const FrameSubmission& submission) noexcept
    {
        if (renderer.vtable.beginFrame && renderer.userData)
        {
            renderer.vtable.beginFrame(renderer.userData, submission);
        }
    }

    // Purpose : Submit render instances to the backend.
    // Contract: instances pointer/count follow the same lifetime rules as the
    //           FrameSubmission that introduced them.
    // Notes   : Null renderer typically ignores the call.
    inline void SubmitInstances(RendererInterface& renderer,
                                const RenderInstance* instances,
                                dng::u32 instanceCount) noexcept
    {
        if (renderer.vtable.submitInstances && renderer.userData && instances && instanceCount > 0)
        {
            renderer.vtable.submitInstances(renderer.userData, instances, instanceCount);
        }
    }

    // Purpose : End the frame for the dynamic interface.
    // Contract: Must be paired with BeginFrame when provided by the backend.
    // Notes   : Safe to call even if the backend omitted the function pointer.
    inline void EndFrame(RendererInterface& renderer) noexcept
    {
        if (renderer.vtable.endFrame && renderer.userData)
        {
            renderer.vtable.endFrame(renderer.userData);
        }
    }

    // Purpose : Resize the rendering surface for swapchain/backbuffer changes.
    // Contract: width/height must be greater than zero. Caller owns
    //           synchronization with any in-flight work.
    // Notes   : Backends may recreate command buffers or attachments.
    inline void ResizeSurface(RendererInterface& renderer, dng::u32 width, dng::u32 height) noexcept
    {
        if (renderer.vtable.resizeSurface && renderer.userData)
        {
            renderer.vtable.resizeSurface(renderer.userData, width, height);
        }
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    // Purpose : Compile-time renderer contract describing the static backend surface.
    // Contract: Backend types must expose capability queries plus Begin/Submit/End/Resize
    //           entry points that are noexcept, allocation-free at the contract edge, and
    //           callable with the exact parameter shapes defined below.
    // Notes   : Backends may allocate internally (e.g., GPU resources) but ownership and
    //           lifetime management stay outside this concept; Core only sees pure views.
    template <typename Backend>
    concept RendererBackend = requires(Backend& backend,
                                       const Backend& constBackend,
                                       const FrameSubmission& submission,
                                       const RenderInstance* instances,
                                       dng::u32 instanceCount,
                                       dng::u32 width,
                                       dng::u32 height)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<RendererCaps>;
        { backend.BeginFrame(submission) } noexcept -> std::same_as<void>;
        { backend.SubmitInstances(instances, instanceCount) } noexcept -> std::same_as<void>;
        { backend.EndFrame() } noexcept -> std::same_as<void>;
        { backend.ResizeSurface(width, height) } noexcept -> std::same_as<void>;
    };

    namespace detail
    {
        template <typename Backend>
        struct InterfaceAdapter
        {
            static RendererCaps GetCaps(const void* userData) noexcept
            {
                return static_cast<const Backend*>(userData)->GetCaps();
            }

            static void BeginFrame(void* userData, const FrameSubmission& submission) noexcept
            {
                static_cast<Backend*>(userData)->BeginFrame(submission);
            }

            static void SubmitInstances(void* userData, const RenderInstance* instances, dng::u32 count) noexcept
            {
                static_cast<Backend*>(userData)->SubmitInstances(instances, count);
            }

            static void EndFrame(void* userData) noexcept
            {
                static_cast<Backend*>(userData)->EndFrame();
            }

            static void ResizeSurface(void* userData, dng::u32 width, dng::u32 height) noexcept
            {
                static_cast<Backend*>(userData)->ResizeSurface(width, height);
            }
        };
    } // namespace detail

    // Purpose : Wrap a static backend into the dynamic RendererInterface.
    // Contract: Backend reference must outlive the interface; no ownership is
    //           transferred. Backend must satisfy RendererBackend concept.
    // Notes   : Typically instantiated once per renderer instance.
    template <typename Backend>
    [[nodiscard]] RendererInterface MakeRendererInterface(Backend& backend,
                                                           RendererBackendKind kind = RendererBackendKind::Unknown) noexcept
    {
        static_assert(RendererBackend<Backend>, "Backend must satisfy RendererBackend concept.");

        RendererInterface iface{};
        iface.userData    = &backend;
        iface.backendKind = kind;
        iface.vtable.getCaps         = &detail::InterfaceAdapter<Backend>::GetCaps;
        iface.vtable.beginFrame      = &detail::InterfaceAdapter<Backend>::BeginFrame;
        iface.vtable.submitInstances = &detail::InterfaceAdapter<Backend>::SubmitInstances;
        iface.vtable.endFrame        = &detail::InterfaceAdapter<Backend>::EndFrame;
        iface.vtable.resizeSurface   = &detail::InterfaceAdapter<Backend>::ResizeSurface;
        return iface;
    }

} // namespace dng::render
