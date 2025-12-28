# Renderer M0 Status

This document captures the renderer state in D-Engine at milestone **M0**. It reflects only the components that currently exist in the repository: the renderer contract, the Null backend, the RendererSystem orchestrator, the BasicForwardRenderer stub, and the associated tests/demos. It does not speculate about future GPU backends or pipelines beyond what is already implemented.

## Current Components

- **Core/Contracts**
  - `Source/Core/Contracts/Renderer.hpp`
    - Defines opaque handle types (`MeshHandle`, `MaterialHandle`, `TextureHandle`, `PipelineHandle`). All are POD wrappers around `u32` with explicit `Invalid()` helpers.
    - Provides data-only structs for frame data (`RenderView`, `RenderInstance`, `FrameSubmission`) plus `RendererCaps`. Every struct is trivially copyable and header-only.
    - Declares `RendererBackendKind` and the `RendererCaps` feature flags (currently all default to `false`).
    - Implements the `RendererBackend` concept that enforces `noexcept` signatures for `GetCaps`, `BeginFrame`, `SubmitInstances`, `EndFrame`, and `ResizeSurface`.
    - Supplies the dynamic interface layer: `RendererVTable`, `RendererInterface`, and helpers (`BeginFrame`, `SubmitInstances`, `EndFrame`, `ResizeSurface`, `QueryCaps`). `MakeRendererInterface` adapts any concept-satisfying backend into this v-table without taking ownership.

- **Core/Renderer**
  - `Source/Core/Renderer/NullRenderer.hpp`
    - Minimal `NullRenderer` backend that satisfies the renderer concept, remembers the last surface width/height, and otherwise ignores submissions.
    - `GetCaps` returns default `RendererCaps` (all features disabled). `BeginFrame` caches the first view dimensions when provided. `SubmitInstances`/`EndFrame` are no-ops. `ResizeSurface` simply stores the requested size.
    - `MakeNullRendererInterface` wraps a `NullRenderer` instance into a `RendererInterface` tagged as `RendererBackendKind::Null`.
  - `Source/Core/Renderer/RendererSystem.hpp`
    - Defines `RendererSystemBackend` (currently `Null` and `Forward`) plus `RendererSystemConfig` and `RendererSystemState`.
    - `RendererSystemState` owns a `RendererInterface`, the active backend enum, an inline `NullRenderer`, and an `isInitialized` flag. No heap allocations occur here.
    - `InitRendererSystem` instantiates the inline `NullRenderer` when `RendererSystemBackend::Null` is requested. Forward backends must instead call `InitRendererSystemWithInterface`, which copies a caller-supplied `RendererInterface` into state without taking ownership.
    - `RenderFrame` issues `BeginFrame → SubmitInstances → EndFrame` on the active interface; `ShutdownRendererSystem` resets state to defaults.

- **Modules/Rendering**
  - `Source/Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp`
    - Header-only stub backend that satisfies `RendererBackend` while remaining allocation-free.
    - Tracks `BasicForwardRendererStats` (fields: `frameIndex`, `lastViewCount`, `lastInstanceCount`, `surfaceWidth`, `surfaceHeight`).
    - `BeginFrame` increments `frameIndex`, records `viewCount`, derives surface size from the first view, and clears `lastInstanceCount`. `SubmitInstances` accumulates instance counts. `ResizeSurface` overrides the cached size. `GetCaps` returns default flags.
    - `MakeBasicForwardRendererInterface` adapts an instance into a `RendererInterface` tagged as `RendererBackendKind::Forward`.

All renderer headers follow the project policy: header-first, `noexcept`, deterministic, and allocation-free at the contract boundary.

## Contract Guarantees at M0

- **Renderer contract (`Renderer.hpp`)**
  - Self-contained header with only engine-absolute includes plus `<concepts>`/`<type_traits>`.
  - Guarantees that all public structs/handles are POD and trivially copyable.
  - The `RendererBackend` concept enforces consistent `noexcept` signatures for every backend.
  - `RendererInterface` is a small value type containing a v-table pointer set, backend kind tag, and opaque user data pointer; ownership and synchronization stay with the caller.

- **RendererSystem**
  - Drives exactly one backend instance via `RenderFrame` without performing allocations or logging.
  - Supports two initialization paths:
    - Internal ownership of a `NullRenderer` via `InitRendererSystem` with `RendererSystemBackend::Null`.
    - External backends injected through `InitRendererSystemWithInterface`, used today by the BasicForwardRenderer demo/tests.
  - Guarantees `ShutdownRendererSystem` can be called safely even if initialization never happened.

- **BasicForwardRenderer**
  - Implements the full backend concept while remaining allocation-free.
  - Telemetry guarantees:
    - `frameIndex` increments once per `BeginFrame` call.
    - `lastViewCount` mirrors the latest `FrameSubmission::viewCount`.
    - `lastInstanceCount` accumulates the `SubmitInstances` counts for the current frame.
    - `surfaceWidth`/`surfaceHeight` track either the last `ResizeSurface` call or the first view in the latest submission.
  - Performs no rendering or stateful GPU work; it only records data flow for observability.

## Non-Goals and Limitations at M0

- No graphics API integration: there is no Direct3D, Vulkan, Metal, or OpenGL code, and no swapchain/window management in the renderer layer.
- No real render pipeline: no visibility determination, batching, materials, shaders, resource streaming, or pass graph implementation yet.
- Single-threaded assumption: `FrameSubmission` is treated as a packed blob supplied by the caller; there is no job system or multi-threaded submission model.
- Minimal `RendererCaps`: all capability flags default to `false`; no backend exposes advanced features yet.
- No engine-wide hookups: rendering does not integrate with input, asset loading, or a full game loop. The only runnable TU is the educational demo that fabricates frame data locally.

## Tests and Demos

- **Header-only self-containment tests (tests/SelfContain/)**
  - `Renderer_header_only.cpp`
  - `Renderer_NullRenderer_header_only.cpp`
  - `Renderer_BasicForwardRenderer_header_only.cpp`
  - `Renderer_RendererSystem_header_only.cpp`
  - These TUs include the corresponding headers in isolation, rely on `static_assert`s (e.g., `RendererBackend<DummyRenderer>`), and compile without defining `main`, ensuring each header is self-contained.

- **Smoke helpers (tests/ and tests/smoke/)**
  - `tests/BasicForwardRenderer_smoke.cpp` → `int RunBasicForwardRendererSmoke()` validates BasicForwardRenderer stats updates after `BeginFrame`/`SubmitInstances`.
  - `tests/RendererSystem_smoke.cpp` → `int RunRendererSystemSmoke()` initializes `RendererSystem` with the inline Null backend and exercises a single `RenderFrame`.
  - `tests/RendererSystem_BasicForwardRenderer_smoke.cpp` → `int RunRendererSystemBasicForwardRendererSmoke()` injects a BasicForwardRenderer through `InitRendererSystemWithInterface`, renders one frame, and inspects telemetry.
  - Additional memory-system smoke helpers reside under `tests/smoke/` and follow the same no-entry-point convention, guaranteeing the renderer layer coexists with other subsystems.

- **Demo**
  - `tests/Renderer_BasicForwardRenderer_demo.cpp` is the only TU with a `main`. It constructs a `BasicForwardRenderer`, wraps it via `MakeBasicForwardRendererInterface`, feeds it into `RendererSystem`, drives three frames with a single view and three dummy instances, and validates every field in `BasicForwardRendererStats` before shutting down the system. The demo allocates nothing and performs no I/O.

Together these tests ensure the renderer contract remains header-first, the backends satisfy the concept, and RendererSystem can drive both Null and BasicForwardRenderer implementations.

## Milestone Definition: Renderer M0

Renderer M0 is considered complete because the following criteria are met in the current codebase:

- The core renderer contract (`Renderer.hpp`) is stable, header-only, and validated by dedicated self-containment TUs.
- Both `NullRenderer` and `BasicForwardRenderer` satisfy the `RendererBackend` concept and can be wrapped via `RendererInterface`.
- `RendererSystem` can either instantiate its own Null backend or accept an externally owned backend (e.g., BasicForwardRenderer) through `InitRendererSystemWithInterface`.
- BasicForwardRenderer exposes deterministic telemetry (`BasicForwardRendererStats`) that is consumed by smoke tests and the demo.
- Renderer-focused tests and the demo build cleanly in Debug/Release configurations with no warnings once linked into the aggregate `D-Engine` target.
- No hidden allocations, exceptions, or RTTI exist in this layer; ownership semantics are explicit and documented.

M0 therefore locks the API shape, lifetime/ownership rules, and telemetry surface for the renderer while explicitly deferring real GPU work to future milestones.

## Future Work (Beyond M0)

The next renderer milestones can build on this foundation by tackling items such as:

- Implementing a real forward renderer backend that talks to an actual graphics API (DX12/Vulkan/Metal) while honoring the existing contract.
- Expanding `RendererCaps` to report concrete capabilities (MSAA levels, HDR formats, mesh shader support) and wiring them through backends.
- Introducing a material/shader abstraction and resource lifetime model behind the renderer contract.
- Adding window/swapchain management via a dedicated display system and integrating it with `ResizeSurface`.
- Enriching `FrameSubmission` handling with multi-view/multi-instance scenarios, including culling hooks and job system integration.
- Extending the smoke/demo coverage to include per-view stats, resize paths, and error-handling scenarios.

(See `Docs/Renderer_Vision.md` for the long-term renderer direction; the list above stays deliberately high-level and grounded in the current code.)
