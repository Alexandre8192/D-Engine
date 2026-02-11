# Time M0 Status

> [!WARNING]
> Historical snapshot: this document describes milestone M0 status at the time it was written and may not match current code.
> For current behavior, see `Docs/Implementation_Snapshot.md`, `D-Engine_Handbook.md`, and `tests/README.md`.

This document captures the time subsystem state at milestone M0. It reflects only the components that currently exist: the time contract, the Null backend, the TimeSystem orchestrator, and the associated tests.

## Current Components
- **Core/Contracts**
  - `Source/Core/Contracts/Time.hpp`
    - Defines `Nanoseconds` plus data-only structs `TimeCaps` and `FrameTime` (all POD/trivially copyable).
    - Declares the `TimeBackend` concept with `GetCaps`, `NowMonotonicNs`, `BeginFrame`, and `EndFrame` all `noexcept`.
    - Provides the dynamic interface: `TimeVTable`, `TimeInterface`, and helpers (`BeginFrame`, `EndFrame`, `NowMonotonicNs`, `QueryCaps`). `MakeTimeInterface` adapts any backend satisfying the concept without taking ownership.
- **Core/Time**
  - `Source/Core/Time/NullTime.hpp`
    - Deterministic backend advancing an internal counter by a fixed step each `NowMonotonicNs` call. No allocations, no logging.
    - Reports `TimeCaps` with `monotonic=true`, `high_res=false`.
    - `MakeNullTimeInterface` wraps an instance into a `TimeInterface`.
  - `Source/Core/Time/TimeSystem.hpp`
    - Defines `TimeSystemBackend` (`Null`, `External`), `TimeSystemConfig`, and `TimeSystemState` (owns `TimeInterface`, inline `NullTime`, last `FrameTime`, `isInitialized`).
    - `InitTimeSystem` resets owned state, configures the Null backend step (`nullStepNs`), validates required function pointers, and primes `FrameTime` with `totalNs` read once from `NowMonotonicNs` (frameIndex=0, deltaNs=0) when `primeOnInit` is true. External backends must be injected via `InitTimeSystemWithInterface`, which validates `userData` and required v-table entries (`nowMonotonic`, `beginFrame`, `endFrame`) and can also prime the baseline.
    - `TickTimeSystem` drives the backend, updates `FrameTime` (frameIndex, deltaNs, totalNs), and remains allocation-free. Deltas are computed against the primed baseline to avoid a first-tick spike on real backends; the first tick yields a positive delta for NullTime due to the fixed step.

## Guarantees
- Header-first, no exceptions, no RTTI, no allocations in the contract or system layer.
- All public structs (`TimeCaps`, `FrameTime`) are POD and trivially copyable.
- `TimeBackend` concept enforces `noexcept` signatures and stable shapes.
- `NullTime` is deterministic and monotonic based on a fixed increment; `TimeSystem` computes non-negative deltas and monotonic totals. On initialization (when priming is enabled), `FrameTime.totalNs` is set to a baseline read so the first tick measures elapsed time from that baseline instead of a large absolute timestamp.

## Non-Goals
- No platform clocks or OS timer integration at M0.
- No wall-clock/calendar/timezone APIs.
- No job-system or multi-threaded timing utilities.
- No profiling/telemetry exporters beyond the minimal `FrameTime` surface.

## Tests
- Header-only compile check: `tests/SelfContain/Time_header_only.cpp` (`static_assert` on `TimeBackend`, interface usage, no main).
- Smoke helper: `tests/Smoke/Subsystems/Time_smoke.cpp` (`RunTimeSmoke()` ticks the Null backend, validates primed baseline (frameIndex=0, deltaNs=0, totalNs>0), then checks frameIndex progression, positive deltas, and strictly monotonic totals; no main).

## Milestone Definition: Time M0
Time M0 is considered complete when:
- The time contract compiles standalone and passes self-contain/static concept assertions.
- NullTime satisfies `TimeBackend`, exposes deterministic monotonic ticks, and is wrapped by `TimeInterface`.
- TimeSystem can either instantiate the Null backend or accept an injected external backend via `InitTimeSystemWithInterface`, and `TickTimeSystem` maintains monotonic `FrameTime`.
- Header-only and smoke tests build cleanly in Debug/Release with warnings treated as errors.
- No hidden allocations, exceptions, or RTTI exist in this layer; ownership semantics are explicit and documented.
