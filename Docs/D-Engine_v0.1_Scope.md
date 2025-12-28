# D-Engine v0.1 Scope (Contract SDK)

## 1) Definition
- v0.1 is a "Contract SDK" milestone: stable contracts, null backends, minimal systems, and validating tests/docs. It is not a feature-complete engine, renderer, or toolchain.
- Deliverables live in-tree: headers under Source/Core/Contracts, corresponding null backends and orchestrators, status docs in Docs/, and smoke/header-only tests in tests/.
- v0.1 prioritizes determinism, header-first compilation, and zero hidden allocations at the contract boundary. No graphics API integration, asset pipeline, job system, or runtime editor is included.

## 2) Subsystem Template (M0 done criteria)
For each subsystem added to v0.1, the following must exist and build cleanly:
- Contract header: `Source/Core/Contracts/<Subsystem>.hpp` (self-contained, POD data, noexcept surface, no hidden allocations).
- Null backend: `Source/Core/<Subsystem>/Null<Subsystem>.hpp` (or equivalent minimal implementation) satisfying the contract without side effects.
- System orchestrator: `Source/Core/<Subsystem>/<Subsystem>System.hpp` (owns or accepts a backend, exposes a unified entry point, no dynamic allocations in this layer).
- Tests: one header-only TU under `tests/SelfContain/` that includes the contract/backends, and one smoke helper under `tests/Smoke/Subsystems/` (per-subsystem) or `tests/Smoke/Memory/` (memory allocators) without `main`, exercising basic flows.
- Status doc: `Docs/<Subsystem>_M0_Status.md` summarizing current behavior, guarantees, and limitations.

## 3) Proposed Subsystem List for v0.1
- Core/Renderer: contract (`Renderer.hpp`), null backend (`NullRenderer.hpp`), system (`RendererSystem.hpp`), BasicForwardRenderer stub in Modules/Rendering for demonstration.
- Core/Memory: allocator contracts and configuration headers; null/backing paths already validated by existing smoke tests (OOM, frame/arena/small-object allocators) and self-contain TUs.
- Core/Math: vector/matrix/quaternion contracts with header-only implementations; validated by self-contain and smoke tests.
- Core/Containers: small engine containers (spans/views, adapters) with header-first policy; verified by self-contain coverage.
- Core/Diagnostics: logger/timer contracts with header-first usage and smoke coverage.
- Modules/Rendering: BasicForwardRenderer stub wired through `RendererInterface` for demo/tests; no GPU API bindings.
- Tools/Tests: smoke harnesses and demos under `tests/` that validate contracts and telemetry without external assets.

## 4) Dependency Policy
- Manifests are the single source of truth for module inclusion and dependencies (project files and future per-module manifest files). No implicit transitive reliance on IDE settings.
- Include hygiene will be enforced by a future checker: only engine-absolute includes allowed; headers must be self-contained; subsystem boundaries must not depend on undeclared modules.

## 5) Release Artifacts and Validation
- Public surface of v0.1: `Source/Core/Contracts/*`, `Source/Core/<Subsystem>/*` null backends and systems, `Source/Modules/Rendering/BasicForwardRenderer/*`, status docs in `Docs/`, and the test/demo sources under `tests/`.
- Validation steps:
  - Build Debug and Release configurations for all targets (D-Engine, Renderer demo, bench runner where applicable).
  - Run demo TU with `main` (e.g., `tests/Renderer_BasicForwardRenderer_demo.cpp`) to confirm telemetry and system wiring.
  - Run smoke/header-only harness (all `tests/Smoke/Subsystems/`, `tests/Smoke/Memory/`, and `tests/SelfContain/` TUs) to ensure contracts are self-contained and backends meet the concept requirements.
- Passing the above with no warnings (treat warnings as errors) constitutes acceptance for v0.1.
