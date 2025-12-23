# Input M0 Status

This document captures the input subsystem state at milestone M0. It reflects only the components that currently exist: the input contract, the Null backend, the InputSystem orchestrator, and the associated tests.

## Current Components
- **Core/Contracts**
  - `Source/Core/Contracts/Input.hpp`
    - Defines `InputDeviceId`, `InputKey`, `InputEventType`, `InputEvent`, and `InputStatus`, all POD/trivially copyable.
    - Declares the `InputBackend` concept with `PollEvents`, `noexcept` and allocation-free at the contract edge.
    - Provides the dynamic interface: `InputVTable`, `InputInterface`, and helper `PollEvents`. `MakeInputInterface` adapts any backend satisfying the concept without taking ownership.
- **Core/Input**
  - `Source/Core/Input/NullInput.hpp`
    - Deterministic backend that always returns `InputStatus::Ok` with zero events. No allocations, no logging.
    - `MakeNullInputInterface` wraps an instance into an `InputInterface`.
  - `Source/Core/Input/InputSystem.hpp`
    - Defines `InputSystemBackend` (`Null`, `External`), `InputSystemConfig`, and `InputSystemState` (owns `InputInterface`, inline `NullInput`, `isInitialized`).
    - `InitInputSystem` resets owned state, instantiates the inline Null backend, and validates injected interfaces in `InitInputSystemWithInterface` (requires `userData` and `pollEvents`). Forwards `PollEvents` while remaining allocation-free.

## Guarantees
- Header-first, no exceptions, no RTTI, no allocations in the contract or system layer.
- Public types are POD/trivially copyable; API uses non-owning views (no std::string).
- `InputBackend` concept enforces `noexcept` signatures and stable shapes.
- Null backend is deterministic and side-effect free; system validation rejects incomplete interfaces.

## Non-Goals
- No event buffering beyond the provided caller-owned array.
- No device discovery, haptics, text input, or key mapping at M0.
- No threading guarantees or async callbacks; polling only.
- No platform-specific key codes or scan code translation in the contract layer.

## Tests
- Header-only compile check: `tests/SelfContain/Input_header_only.cpp` (`static_assert` on `InputBackend`, interface usage, no main).
- Smoke helper: `tests/Input_smoke.cpp` (`RunInputSmoke()` initializes the system with the Null backend and expects `PollEvents` to return `Ok` with zero events; no main).

## Milestone Definition: Input M0
Input M0 is considered complete when:
- The input contract compiles standalone and passes self-contain/static concept assertions.
- NullInput satisfies `InputBackend`, remains deterministic, and is wrapped by `InputInterface`.
- InputSystem can instantiate the Null backend or accept an injected external backend via `InitInputSystemWithInterface`, validating required function pointers.
- Header-only and smoke tests build cleanly in Debug/Release with warnings treated as errors.
- No hidden allocations, exceptions, or RTTI exist in this layer; ownership and error signaling are explicit and documented.
