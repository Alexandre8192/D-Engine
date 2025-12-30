# D-Engine - Determinism Policy

## Purpose
Define what "determinism" means in D-Engine, what is guaranteed, what is not,
and which rules subsystems must follow to keep simulation behavior reproducible.

This document is an engine-level policy. Subsystems MUST reference it and
declare their determinism support in their own contracts.

## Scope
This policy applies to:
- Simulation logic (gameplay, physics, AI, animation state machines, etc.)
- Engine orchestration that affects simulation results (ordering, scheduling, RNG, time)

This policy does NOT require bitwise identical results across different machines.

## Definitions
- Simulation: the state evolution that must be reproducible for debugging/tests.
- Replay: the ability to rerun the same simulation and obtain the same results.
- Deterministic output: simulation outputs are stable when driven by the same
  recorded inputs and the same deterministic time/RNG sources.

## Determinism Levels
D-Engine supports multiple determinism modes. The mode is a runtime configuration
(plus potentially build-time switches for stricter enforcement).

### Mode: Off
- Goal: performance-first.
- Guarantee: none.
- Notes: results may differ run-to-run if unordered iteration, opportunistic job
  scheduling, or platform-specific behavior is used.

### Mode: Replay (Default for Debug/Tests)
- Goal: reproducible simulation for debugging and CI tests.
- Guarantee: same inputs + same build + same machine => same simulation outputs.
- Additional notes:
  - Simulation runs with deterministic time steps (SimulationClock).
  - RNG usage must be explicit and seeded deterministically.
  - Iteration/order must be stable where it affects results.
  - Simulation runs single-thread by default (see Threading Rules).

### Mode: Strict (Experimental)
- Goal: stronger reproducibility guarantees via stricter restrictions.
- Guarantee: not promised by default.
- Notes: may require additional constraints (float policy, deterministic job
  scheduling, forbidding some math functions, quantization/fixed-point, etc.).

## Core Guarantees (Replay Mode)
In Replay mode:
1) Simulation time is derived ONLY from SimulationClock.
2) All randomness is explicit:
   - No global RNG state.
   - No implicit seeding.
   - Seeds are recorded or derived deterministically from known IDs.
3) No simulation result may depend on:
   - OS time
   - thread timing
   - pointer addresses
   - unordered container iteration
   - nondeterministic job completion order
4) Multithreaded work that influences simulation MUST follow Threading Rules:
   - jobs write to private lanes
   - deterministic merge in stable order

## Subsystem Requirements
Each subsystem contract MUST declare:
- SupportsReplayDeterminism: yes/no
- SupportsStrictDeterminism: yes/no
- DeterminismNotes: known nondeterminism sources and required mitigations
- ThreadSafetyNotes: what the caller must guarantee

Example declarations (conceptual):
- Audio: Replay determinism guarantees event timeline stability, not sample-bitwise identity.
- Renderer: Replay determinism may be off-scope for visuals; must not affect simulation state.
- IO/Streaming: must not feed nondeterministic data into simulation without recording.

## Stable Ordering Rules (Replay)
If iteration order affects results, it MUST be stable.
Allowed strategies:
- Iterate over arrays/vectors with stable order.
- Use sorted views of keys/handles/IDs before applying effects.
- Merge per-entity/per-chunk buffers in ascending ID order.

Disallowed strategies:
- Relying on hash container iteration order.
- Relying on pointer address ordering.
- Relying on "who finishes first" in job systems.

## Testing and Verification
Replay determinism is only meaningful if tested.

Minimum recommended test:
- Run N simulation ticks (e.g., 300-2000).
- Feed a deterministic CommandBuffer per tick.
- Compute a stable hash of simulation state every M ticks.
- Compare against a baseline.

Notes:
- Hash must be stable and not depend on addresses or unordered iteration.
- Baselines are per-platform/per-build configuration as needed.

## Non-goals (for now)
- Bitwise identical simulation across different machines.
- Bitwise identical floating-point results across different compilers/flags.
- Player-facing replay features (UI, timeline, seeking, compression).

## FAQ
Q: Is determinism compatible with multithreading?
A: Yes, but only with strict rules: jobs must not directly mutate shared simulation
   state in a nondeterministic order. Use private buffers and deterministic merges.

Q: Why is Replay mode single-thread by default?
A: It provides a reliable "oracle" for debugging and CI. It minimizes complexity
   and isolates bugs caused by data races or order-dependent merges.
