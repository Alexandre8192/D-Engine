# Contract SDK v0.1 Progress Summary

## Overview
v0.1 locks a header-first "Contract SDK" for core engine services—rendering, time, filesystem, window, and input—using allocation-free contracts, deterministic Null backends, and small orchestrator systems, with dynamic interfaces (vtable + wrapper) to host injected implementations.

## Completed milestones
- Renderer M0: Contract defined in [Source/Core/Contracts/Renderer.hpp](Source/Core/Contracts/Renderer.hpp); Null backend in [Source/Core/Renderer/NullRenderer.hpp](Source/Core/Renderer/NullRenderer.hpp); system orchestration in [Source/Core/Renderer/RendererSystem.hpp](Source/Core/Renderer/RendererSystem.hpp); BasicForwardRenderer stub in [Source/Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp](Source/Modules/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp); status doc [Docs/Renderer_M0_Status.md](Docs/Renderer_M0_Status.md); smoke helpers [tests/BasicForwardRenderer_smoke.cpp](tests/BasicForwardRenderer_smoke.cpp), [tests/RendererSystem_smoke.cpp](tests/RendererSystem_smoke.cpp), [tests/RendererSystem_BasicForwardRenderer_smoke.cpp](tests/RendererSystem_BasicForwardRenderer_smoke.cpp); demo main [tests/Renderer_BasicForwardRenderer_demo.cpp](tests/Renderer_BasicForwardRenderer_demo.cpp).
- Time M0: Contract in [Source/Core/Contracts/Time.hpp](Source/Core/Contracts/Time.hpp); Null backend in [Source/Core/Time/NullTime.hpp](Source/Core/Time/NullTime.hpp); system orchestration (with `primeOnInit` and `nullStepNs` config, baseline priming) in [Source/Core/Time/TimeSystem.hpp](Source/Core/Time/TimeSystem.hpp); status doc [Docs/Time_M0_Status.md](Docs/Time_M0_Status.md); smoke helper [tests/Time_smoke.cpp](tests/Time_smoke.cpp).
- FileSystem M0: Contract in [Source/Core/Contracts/FileSystem.hpp](Source/Core/Contracts/FileSystem.hpp); Null backend in [Source/Core/FileSystem/NullFileSystem.hpp](Source/Core/FileSystem/NullFileSystem.hpp); system orchestrator in [Source/Core/FileSystem/FileSystemSystem.hpp](Source/Core/FileSystem/FileSystemSystem.hpp); status doc [Docs/FileSystem_M0_Status.md](Docs/FileSystem_M0_Status.md); smoke helper [tests/FileSystem_smoke.cpp](tests/FileSystem_smoke.cpp).
- Window M0: Contract in [Source/Core/Contracts/Window.hpp](Source/Core/Contracts/Window.hpp); Null backend in [Source/Core/Window/NullWindow.hpp](Source/Core/Window/NullWindow.hpp); system orchestrator in [Source/Core/Window/WindowSystem.hpp](Source/Core/Window/WindowSystem.hpp); status doc [Docs/Window_M0_Status.md](Docs/Window_M0_Status.md); smoke helper [tests/Window_smoke.cpp](tests/Window_smoke.cpp).
- Input M0: Contract in [Source/Core/Contracts/Input.hpp](Source/Core/Contracts/Input.hpp); Null backend in [Source/Core/Input/NullInput.hpp](Source/Core/Input/NullInput.hpp); system orchestrator in [Source/Core/Input/InputSystem.hpp](Source/Core/Input/InputSystem.hpp); status doc [Docs/Input_M0_Status.md](Docs/Input_M0_Status.md); smoke helper [tests/Input_smoke.cpp](tests/Input_smoke.cpp).

## Tests and validation
- Smoke helpers follow the "no main" pattern: functions like `RunRendererSystemSmoke`, `RunTimeSmoke`, `RunFileSystemSmoke`, `RunWindowSmoke`, `RunInputSmoke`, and `RunBasicForwardRendererSmoke` live in [tests/](tests/) and [tests/smoke/](tests/smoke/), returning 0/!0 to signal pass/fail without defining `main`.
- The only entry-point TU for the contract stack is the demo in [tests/Renderer_BasicForwardRenderer_demo.cpp](tests/Renderer_BasicForwardRenderer_demo.cpp), wiring `BasicForwardRenderer` through `RendererSystem` and asserting telemetry over three frames.
- Header-only self-containment translation units in [tests/SelfContain/](tests/SelfContain/) (e.g., [tests/SelfContain/Renderer_header_only.cpp](tests/SelfContain/Renderer_header_only.cpp), [tests/SelfContain/Time_header_only.cpp](tests/SelfContain/Time_header_only.cpp), [tests/SelfContain/FileSystem_header_only.cpp](tests/SelfContain/FileSystem_header_only.cpp), [tests/SelfContain/Window_header_only.cpp](tests/SelfContain/Window_header_only.cpp), [tests/SelfContain/Input_header_only.cpp](tests/SelfContain/Input_header_only.cpp)) include each header alone with `static_assert` concept checks to guarantee standalone compilation.

## Guarantees and policies enforced
- Header-first, `noexcept`, no exceptions/RTTI, and no hidden allocations at the contract boundary across renderer, time, filesystem, window, and input headers and systems.
- Dynamic interface pattern (concept + vtable + small interface wrapper) is applied consistently for each subsystem, enabling external backends to be injected without ownership transfer.
- Public structs/enums are POD/trivially copyable and use non-owning views; Null backends are deterministic and allocation-free; system init paths validate required vtable entries and allow Null or injected backends.

## Known limitations / non-goals (current)
- No real GPU backend, swapchain, or rendering pipeline; only `NullRenderer` and the telemetry-only `BasicForwardRenderer` stub.
- Windowing has no OS integration or real window creation beyond the simulated handle in `NullWindow`.
- FileSystem is Null-only and read-oriented; no writes, iteration, or platform file metadata.
- Input is poll-only and Null-only; no device discovery, text input, or haptics.

## What changed (key additions)
- Added header-only contracts and POD data surfaces for Renderer/Time/FileSystem/Window/Input with enforced `noexcept` concepts.
- Implemented deterministic Null backends for each contract plus orchestration systems to own or accept injected interfaces.
- Introduced the BasicForwardRenderer stub backend and wired it through `RendererSystem` alongside telemetry checks.
- Established smoke-test harnesses (`Run*Smoke` helpers) without entry points and a single demo `main` exercising the renderer path.
- Added self-containment TUs under tests/SelfContain to ensure each public header compiles in isolation.
