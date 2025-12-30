# D-Engine - Time Policy

## Purpose
Define how time is represented and consumed in D-Engine, with a focus on:
- deterministic simulation updates
- variable-rate rendering
- reproducible debug/tests (Replay mode)

## Scope
Applies to:
- simulation tick scheduling
- time sources (real vs simulation)
- interpolation strategy for rendering
- time-dependent gameplay systems (cooldowns, timers, physics integration)

## Clocks
D-Engine distinguishes two time sources:

### SimulationClock (Deterministic)
- Used by simulation logic.
- Advances in fixed steps (fixed delta).
- In Replay mode, SimulationClock is the only allowed time source for simulation.

Properties:
- TickIndex: monotonic integer tick counter (0, 1, 2, ...)
- FixedDeltaSeconds: constant step size (default 1/60)
- SimTimeSeconds: derived = TickIndex * FixedDeltaSeconds

### RealClock (Nondeterministic)
- Used by tools, editor UI, profiling display, wall-clock timers, etc.
- Derived from OS time and therefore not deterministic.
- MUST NOT drive simulation decisions in Replay mode.

## Fixed-Step Simulation (Required for Replay)
Simulation runs using a fixed time step and an accumulator.

High-level loop:
- real_dt = RealClock delta
- accumulator += real_dt
- while accumulator >= fixed_dt:
    - BuildCommandBufferForTick()
    - SimulationStep(fixed_dt)
    - accumulator -= fixed_dt
- render_alpha = accumulator / fixed_dt
- RenderInterpolate(render_alpha)

Notes:
- SimulationStep must only depend on:
  - SimulationClock (tick index and fixed_dt)
  - recorded inputs/commands for that tick
  - explicit RNG
- Render may interpolate between previous and current simulation states.

## Default Fixed Step
Recommended engine default:
- FixedDeltaSeconds = 1/60

Rationale:
- good balance for CPU cost and responsiveness
- common baseline for many realtime games

Project-level configuration may choose 1/120 for high-demand action titles.
Engine policy: fixed-step is configurable; the existence of fixed-step is not optional.

## Input Sampling Policy (Repro Friendly)
For realtime action:
- Sample raw input as late as possible before each simulation tick.
- Convert raw input into a deterministic CommandBuffer for that tick.
- Simulation consumes CommandBuffer only.

In Replay mode:
- CommandBuffers are recorded or generated deterministically.
- Raw OS events are not used as a direct simulation input source.

## Timers and Cooldowns
Timers in simulation should be expressed in:
- ticks (preferred), or
- fixed-step time derived from ticks

Avoid:
- comparing against RealClock time
- fractional time accumulation that depends on variable dt

Recommended:
- store "end tick" for cooldowns: EndTick = CurrentTick + DurationTicks
- compute remaining time from tick difference if needed for UI

## Floating-Point Considerations
Even with fixed-step, floating-point behavior can drift if ordering changes.
Time policy helps by keeping dt constant and removing a major source of drift.

In Strict mode (experimental), additional float constraints may apply.

## Editor and Tools
The editor may use RealClock for UI and interaction.
When the editor drives simulation:
- it should advance SimulationClock deterministically (fixed-step)
- it should not inject RealClock into simulation decisions

This separation reduces tool instability impact on runtime determinism.

## Logging and Profiling
- Profiling timestamps can use RealClock.
- Simulation logs should tag events with TickIndex for reproducibility.
- Avoid mixing wall time into simulation event ordering.

## Summary
- Simulation uses SimulationClock (fixed-step, deterministic).
- Rendering uses RealClock for pacing and interpolates using alpha.
- Replay mode forbids RealClock in simulation logic.
