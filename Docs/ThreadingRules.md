# D-Engine - Threading Rules

## Purpose
Define the rules that make multithreading compatible with Replay determinism.
These rules primarily protect simulation correctness and reproducibility.

This document is normative for all subsystems that affect simulation state.

## Scope
Applies to:
- Job systems and task scheduling
- Any parallel work that contributes to simulation results
- Data access patterns in simulation updates

Does not constrain:
- Rendering preparation that does not feed back into simulation state
- IO pipelines that are purely asynchronous and do not alter simulation logic
  (unless their results are injected into simulation, in which case the injection
  must be deterministic/recorded)

## Core Principle
In Replay mode, parallelism must be structured so that:
- The result does not depend on execution timing, and
- The final applied effects are merged in a deterministic order.

If you cannot prove that, do not do it in Replay mode.

## Ownership and Mutation
Simulation state is owned by systems or phases. Mutations must be controlled.

Rules:
1) Avoid shared mutable state between jobs.
2) If multiple jobs contribute to the same logical output, use a deterministic merge.
3) Do not use atomics as "gameplay logic ordering" in Replay mode.

## Allowed Patterns (Recommended)
### Pattern A: Write to Your Own Lane + Deterministic Merge
- Each job writes to a private buffer:
  - per-job buffer
  - per-entity buffer
  - per-chunk buffer
- After all jobs complete, a merge step applies results in stable order:
  - ascending EntityId
  - ascending HandleId
  - ascending ChunkId

Examples:
- Physics: each job computes contacts/impulses into per-chunk arrays; merge applies
  in chunk order, then within chunk by stable entity order.
- AI: each job emits "intent" commands; merge applies intents in sorted entity order.

### Pattern B: Stable Reduction Trees
If you must reduce values (sum/min/max), ensure the reduction order is stable.
- Use a deterministic reduction tree (fixed partitioning, stable pairings).
- Avoid opportunistic batching that changes run-to-run.

Important note: floating-point addition is not associative; stable order matters.

### Pattern C: Parallel Work with Order-Independent Operations
If operations are truly order-independent and commutative (e.g., integer bitsets OR),
they may be parallelized with care.
- Still prefer deterministic merges to avoid subtle issues.

## Forbidden Patterns in Replay Mode
- Multiple jobs writing directly to the same simulation variable/state without
  deterministic ordering.
- Using "who finishes first" as a logical decision.
- Parallel reductions over floats without stable ordering.
- Mutating shared containers from multiple threads without strict ownership and
  deterministic merging.
- Relying on:
  - thread ids
  - OS scheduling
  - address ordering
  - unordered container iteration order

## Container and Iteration Rules
If iteration order affects simulation results:
- DO:
  - use stable containers (arrays/vectors)
  - sort key lists before applying effects
  - keep stable ID assignment and stable ordering

- DO NOT:
  - iterate directly over hash maps/sets and apply effects in that order
  - iterate over pointer-based containers with nondeterministic ordering

If you need a map in simulation code:
- prefer an ordered map or a sorted view of keys
- or keep hash maps for lookup only, and separately maintain a stable key list

## Barriers and Phases
Replay mode must have deterministic synchronization points.
- Use explicit phase boundaries (barriers) where merges occur.
- Avoid implicit dependencies hidden behind locks or background threads.

Recommended structure per simulation tick:
1) Read-only gather phase (parallel OK)
2) Compute phase producing per-lane outputs (parallel OK)
3) Deterministic merge/apply phase (single-thread by default in Replay)
4) Post-tick validation/invariants (single-thread)

## Scheduling Rules (Replay)
In Replay mode:
- Job submission order must be stable.
- Partitioning must be stable (fixed chunk sizes or deterministic chunking rules).
- No auto-tuning that changes partitioning without being recorded.

Default recommendation:
- Replay simulation runs single-thread.
- Parallelism in Replay is only enabled when the engine provides a proven
  deterministic scheduler and merge framework.

## Diagnostics
When possible, provide debug checks:
- detect direct shared-state writes from jobs in Replay mode
- assert stable ordering assumptions in merges
- optional tracing that records merge order for debugging

## Rationale
These rules prevent two major classes of bugs:
1) Data races and nondeterministic ordering causing hard-to-repro issues.
2) Floating-point and ordering effects causing Replay drift across runs.
