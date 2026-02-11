# Window M0 Status

> [!WARNING]
> Historical snapshot: this document describes milestone M0 status at the time it was written and may not match current code.
> For current behavior, see `Docs/Implementation_Snapshot.md`, `D-Engine_Handbook.md`, and `tests/README.md`.

This document captures the window subsystem state at milestone M0. It reflects only the components that currently exist: the window contract, the Null backend, the WindowSystem orchestrator, and the associated tests.

## Current Components
- **Core/Contracts**
  - `Source/Core/Contracts/Window.hpp`
    - Defines `TitleView`, `WindowHandle`, `WindowDesc`, `WindowEventType`, `WindowEvent`, and `WindowStatus`, all POD/trivially copyable.
    - Declares the `WindowBackend` concept with `CreateWindow`, `DestroyWindow`, `PollEvents`, and `GetSurfaceSize`, all `noexcept` and allocation-free at the contract edge.
    - Provides the dynamic interface: `WindowVTable`, `WindowInterface`, and helpers (`CreateWindow`, `DestroyWindow`, `PollEvents`, `GetSurfaceSize`). `MakeWindowInterface` adapts any backend satisfying the concept without taking ownership.
- **Core/Window**
  - `Source/Core/Window/NullWindow.hpp`
    - Deterministic backend simulating a single window handle (value=1), storing width/height from the last creation, reporting `Ok` for valid handle queries, and producing zero events. No allocations, no logging.
    - `MakeNullWindowInterface` wraps an instance into a `WindowInterface`.
  - `Source/Core/Window/WindowSystem.hpp`
    - Defines `WindowSystemBackend` (`Null`, `External`), `WindowSystemConfig`, and `WindowSystemState` (owns `WindowInterface`, inline `NullWindow`, `isInitialized`).
    - `InitWindowSystem` resets owned state, instantiates the inline Null backend, and validates injected interfaces in `InitWindowSystemWithInterface` (requires `userData` and all v-table entries). Forwards calls while remaining allocation-free.

## Guarantees
- Header-first, no exceptions, no RTTI, no allocations in the contract or system layer.
- Public types are POD/trivially copyable; API uses non-owning views (no std::string).
- `WindowBackend` concept enforces `noexcept` signatures and stable shapes.
- Null backend is deterministic and side-effect free; system validation rejects incomplete interfaces.

## Non-Goals
- No platform window integration (no OS message pumps, swapchains, or input) at M0.
- No multi-window management beyond a single simulated handle.
- No DPI/scaling, monitor queries, or fullscreen toggles.
- No asynchronous event queues or threading model guarantees.

## Tests
- Header-only compile check: `tests/SelfContain/Window_header_only.cpp` (`static_assert` on `WindowBackend`, interface usage, no main).
- Smoke helper: `tests/Smoke/Subsystems/Window_smoke.cpp` (`RunWindowSmoke()` initializes the system with the Null backend, creates a dummy window, queries surface size, polls events (expect zero), destroys the window, then shuts down; no main).

## Milestone Definition: Window M0
Window M0 is considered complete when:
- The window contract compiles standalone and passes self-contain/static concept assertions.
- NullWindow satisfies `WindowBackend`, remains deterministic, and is wrapped by `WindowInterface`.
- WindowSystem can instantiate the Null backend or accept an injected external backend via `InitWindowSystemWithInterface`, validating required function pointers.
- Header-only and smoke tests build cleanly in Debug/Release with warnings treated as errors.
- No hidden allocations, exceptions, or RTTI exist in this layer; ownership and error signaling are explicit and documented.
