# D-Engine Handbook (Single Source of Truth)

> This file is the canonical place for D-Engine's vision, non-negotiables,
> technical policies, and roadmap.
>
> Other documents that used to define policies/roadmaps now act as thin
> pointers to sections of this handbook to avoid contradictions.

Last updated: 2026-02-14

-------------------------------------------------------------------------------

## How to read this handbook

This document is intentionally long. It exists to prevent "wishful architecture"
and to keep D-Engine coherent as it grows.

### Read it in layers

1) **Product charter + reality checks** (Sections 0-2)
   - What D-Engine is trying to be.
   - What it explicitly does NOT promise.
   - The chosen wedge (Crowd-first Musou performance) and why it is realistic.

2) **Principles + architecture** (Sections 3-6)
   - The non-negotiables.
   - The canonical "Contract / Null backend / System" model.
   - How modules, layering, and costs are meant to work.

3) **Normative policies** (Sections 7-16)
   - These are the rules that keep the codebase stable and predictable.
   - If a rule says MUST/SHALL, it is enforced (lint/tests/CI) or treated as a bug.

4) **Roadmap + implementation snapshot** (Sections 17-20)
   - Roadmap: where we want to go next (planning).
   - Snapshot: what the code actually does today (reality).

### Suggested reading paths

- **New to the project (10-15 min):**
  Read 0-3, skim 4-6, then jump to 17 (Roadmap).

- **Implementing a subsystem or backend (30-60 min):**
  Read 4-6, then the relevant policies (8-16), then 19 (Review checklist).

- **Interested in determinism / replay / rollback:**
  Read 10-12 carefully, plus the enforcement notes in 16.

- **Interested in plugins or Rust interop:**
  Read 14-15, then the ABI section in 16 (bench + test expectations still apply).

### What to trust when things disagree

- This handbook is the **single source of truth** for intent and rules.
- The implementation snapshot is the **single source of truth** for current code.
- If they disagree, treat it as a bug in documentation: update one side until they match.

### Terms used in this doc (quick glossary)

- **Contract**: a backend-agnostic API surface with explicit ownership, error behavior,
  determinism notes, and a visible cost model.
- **Backend**: a concrete implementation of a contract (Null, Win32, DX12, etc.).
- **System**: the orchestrator that owns lifecycle and ties contracts together safely.
- **Replay mode**: a configuration where simulation is structured to be reproducible.
- **Wedge**: a narrow, demonstrable use case used to prove value (Crowd-first Musou).

-------------------------------------------------------------------------------

## Table of contents

0. [Why D-Engine exists (product charter)](#0-why-d-engine-exists-product-charter)
1. [The wedge: Crowd-first (Musou) performance as proof](#1-the-wedge-crowd-first-musou-performance-as-proof)
2. [What D-Engine does NOT promise (explicit non-goals)](#2-what-d-engine-does-not-promise-explicit-non-goals)
3. [Guiding principles (the "dream engine" rules)](#3-guiding-principles-the-dream-engine-rules)
4. [Architecture model](#4-architecture-model)
5. [Repository layout and layering](#5-repository-layout-and-layering)
6. [Contracts-first rules](#6-contracts-first-rules)
7. [Policy matrix (map of all rules)](#7-policy-matrix-map-of-all-rules)
8. [Core language policy (no exceptions, no RTTI, STL constraints)](#8-core-language-policy-no-exceptions-no-rtti-stl-constraints)
9. [Header-first strategy (fast build, thin facades)](#9-header-first-strategy-fast-build-thin-facades)
10. [Determinism policy](#10-determinism-policy)
11. [Threading rules (Replay determinism compatible)](#11-threading-rules-replay-determinism-compatible)
12. [Time policy (SimulationClock vs RealClock)](#12-time-policy-simulationclock-vs-realclock)
13. [Memory policy (defaults, precedence, invariants)](#13-memory-policy-defaults-precedence-invariants)
14. [ABI and interop policy (stable C ABI for modules)](#14-abi-and-interop-policy-stable-c-abi-for-modules)
15. [ABI review checklist](#15-abi-review-checklist)
16. [Benchmark protocol](#16-benchmark-protocol)
17. [Roadmap (two tracks: SDK stability + crowd slice)](#17-roadmap-two-tracks-sdk-stability--crowd-slice)
18. [Implementation snapshot (code-backed reality)](#18-implementation-snapshot-code-backed-reality)
19. [Contribution workflow and review checklist](#19-contribution-workflow-and-review-checklist)
20. [Appendix: historical snapshots and where to look next](#20-appendix-historical-snapshots-and-where-to-look-next)

-------------------------------------------------------------------------------

## 0. Why D-Engine exists (product charter)

D-Engine is a programmer-first engine project built around four promises:

1) **Contracts-first**: every subsystem starts as a backend-agnostic contract
   with explicit ownership, error behavior, determinism notes, and a visible cost
   model.
2) **Determinism and auditability**: the engine is structured so that simulation
   can be made reproducible (Replay mode) and debugged with stable ordering.
3) **Header-first clarity**: the public surface is readable and self-contained.
   "What does this do and what does it cost?" should be answerable by reading a
   header.
4) **Performance by design, not by luck**: no hidden allocations on hot paths,
   explicit data layouts, and benchmarks treated as first-class tests.

Platform scope (current): **Windows-only**.

This file also contains explicit reality checks. The goal is to avoid
"wishful architecture" that feels inspiring but cannot ship.

-------------------------------------------------------------------------------

## 1. The wedge: Crowd-first (Musou) performance as proof

D-Engine does not need to "beat Unreal/Unity at everything" to be valuable.
The strategy is to win hard on a narrow, concrete use case that:

- is easy to demonstrate,
- is easy to benchmark,
- benefits from determinism and low-latency scheduling,
- and is painful in general-purpose engines.

### 1.1 The chosen wedge

**Crowd-first action (Musou / Dynasty Warriors-like)**:

- hundreds to thousands of animated agents,
- lots of VFX, collisions, and gameplay logic,
- stable 60 fps target,
- minimal stutter (frame-time stability matters more than average fps).

This wedge is compatible with D-Engine's core philosophy:
data-oriented simulation, deterministic-friendly update rules, and explicit cost.

### 1.2 What "winning" looks like

The first believable win is NOT "a full engine". It is a reproducible proof:

- a fixed benchmark scene,
- a stable simulation tick,
- agent update + animation sampling + visibility/VFX submission,
- a frame-time graph that stays flat under load,
- and a clearly explained cost model.

If this proof is strong, it becomes a marketing and adoption lever. It also
forces the engine to become real (windowing, input, jobs, renderer path, asset
loading), but in a scoped way.

### 1.3 Why this wedge can plausibly differentiate

General-purpose engines pay overhead for:

- broad feature sets,
- editor-first pipelines,
- dynamic reflection/serialization,
- flexible but expensive entity models,
- multi-purpose job scheduling.

D-Engine can choose to be:

- narrow in scope at first,
- explicit about data and ownership,
- strict about determinism/stable ordering,
- and willing to optimize for one scenario.

This does not guarantee superiority, but it makes a measurable win plausible.

-------------------------------------------------------------------------------

## 2. What D-Engine does NOT promise (explicit non-goals)

These non-goals are part of the charter. They are here to reduce future doubt
and prevent the roadmap from drifting into impossibility.

### 2.1 Not a "Unreal replacement" across all use cases

D-Engine aims to be a serious alternative for some teams and some genres.
It is not designed to cover the full Unreal/Unity surface area.

### 2.2 Not instantly studio-ready

Large studios choose engines based on:

- shipping track record,
- tooling maturity,
- long-term support guarantees,
- stability (API/ABI),
- debug/profiling pipelines,
- hiring and onboarding cost.

D-Engine can become attractive over time, but "studio-ready" is a result of
multiple shipped projects, not a document.

### 2.3 Not designer-friendly "by magic"

AI assistants help, but a designer-friendly engine requires:

- robust tooling (editor, content pipeline),
- iteration workflows,
- safe scripting or visual logic,
- documentation and UX.

D-Engine is intentionally programmer-first. Designer friendliness can be added
later, but it is not a v0.x requirement.

### 2.4 Not bitwise identical simulation across all machines

Replay determinism is defined in this handbook, but "bitwise identical across
different hardware and compilers" is out of scope by default.

-------------------------------------------------------------------------------

## 3. Guiding principles (the "dream engine" rules)

These are the non-negotiables that shape every subsystem.

### 3.1 Contracts first, backends second

- Start every subsystem by writing the contract.
- The contract must be readable, stable, and explicit.
- Then implement:
  - a Null backend (deterministic reference),
  - a System/orchestrator layer,
  - and only then a real platform backend.

### 3.2 No hidden allocations and visible costs

- Public APIs must document allocation behavior.
- Hot paths must avoid hidden allocations.
- Prefer caller-provided buffers or explicit arenas.

### 3.3 Determinism as a first-class mode

- Replay mode exists to make bugs reproducible.
- Stable ordering rules are mandatory when results depend on order.
- Multithreading is allowed only with deterministic merge rules.

### 3.4 Header-first, self-contained public surface

- Contracts are in headers.
- Every public header compiles in isolation.
- "Thin facade" pattern: keep heavy templates in `detail/` and `.cpp`.

### 3.5 Minimal magic, maximum clarity

- Avoid opaque macros.
- Prefer explicit types, explicit ownership, explicit error returns.
- Teach through code: Purpose/Contract/Notes are expected.

### 3.6 Modular by construction

- A project can pick which backends to use.
- The engine should not force a single "one true" implementation.
- If a backend respects the contract, it should plug in.

-------------------------------------------------------------------------------

## 4. Architecture model

The engine is organized around three layers:

1) **Contract layer** (source-level, C++):
   - backend-agnostic types and functions
   - stable shape for in-repo backends
2) **System layer** (C++):
   - orchestrates initialization, lifetime, validation, and dispatch
   - owns "policy" decisions (Replay mode, error handling, threading mode)
3) **Backend layer**:
   - Null backend (reference)
   - platform backend (Win32, audio device, etc.)
   - external backend (third-party)
   - optional loadable module via C ABI

This separation keeps the Core understandable and makes evolution safer.

-------------------------------------------------------------------------------

## 5. Repository layout and layering

High-level map:

- `Source/Core/Contracts/` : subsystem contracts (public API)
- `Source/Core/<Subsystem>/` : system + null backend
- `Source/Core/Abi/` : stable C ABI headers
- `Source/Core/Interop/` : C++ wrappers around ABI
- `Source/Modules/` : optional modules/backends
- `tests/` : header self-containment + smoke tests + bench runner
- `Docs/` : thin pointers and historical snapshots

Core rule: **Contracts live in `Source/Core/Contracts/`**.

-------------------------------------------------------------------------------

## 6. Contracts-first rules

Every contract header must contain a file header block like:

```cpp
// ============================================================================
// D-Engine - <Module>/<Path>/<File>.hpp
// ----------------------------------------------------------------------------
// Purpose : <big-picture intent>
// Contract: <inputs/outputs/ownership; thread-safety; noexcept; allocations;
//           determinism; compile-time/runtime guarantees>
// Notes   : <rationale; pitfalls; alternatives; references; cost model>
// ============================================================================
```

Contract requirements (minimum):

- **Ownership**: who owns what, who frees what.
- **Error model**: status codes, no exceptions.
- **Thread-safety**: what is safe, what is not.
- **Determinism notes**: what is stable in Replay mode.
- **Allocation model**: explicit call-outs for any allocation.
- **Ordering guarantees**: if ordering affects results, define it.

-------------------------------------------------------------------------------

## 7. Policy matrix (map of all rules)

This is the engine's policy map. It is repeated here so there is one place to
read everything.

| Policy | Scope | Requirement (one-liner) | Enforcement |
| --- | --- | --- | --- |
| Core language policy | Core C++ usage | No exceptions/RTTI in Core; allocator-aware STL only; explicit status-based APIs | Build flags + lint + review |
| Header-first strategy | Public headers/build hygiene | Contracts in self-contained headers; heavy templates stay out of the public inclusion cone | Header self-contain tests + review |
| Determinism policy | Simulation determinism | Replay mode uses deterministic time/RNG and stable ordering; no nondeterministic sources in simulation paths | Replay tests + review |
| Threading rules | Parallel simulation work | Jobs write to private lanes and merge deterministically; forbid timing/order-dependent patterns in Replay | Replay tests + review |
| Time policy | Time sources/ticks | Simulation uses fixed-step SimulationClock; RealClock only for rendering/tools; command buffers per tick | Design review + tests |
| Memory policy | Allocators/defaults | No hidden allocs in hot paths; precedence rules (API -> env -> macros); stable invariants | Smokes + benches + review |
| ABI and interop policy | Cross-language boundary | Stable C ABI only, function tables + POD; explicit ownership; no unwind; versioned v1+ | ABI smoke tests + review |
| ABI review checklist | ABI change gate | Any ABI change must pass checklist items A-G | Review gate |
| Benchmark protocol | Perf regression detection | Repro-stable bench execution + JSON outputs + CI comparisons | Bench CI + review |

-------------------------------------------------------------------------------

## 8. Core language policy (no exceptions, no RTTI, STL constraints)

Goal: deterministic, auditable, portable C++23/26 with zero hidden costs.

### 8.1 Exceptions

Policy:

- Forbidden in `Source/Core/**`: no `throw`, no `try/catch`.
- Allowed only at interop boundaries (separate module) to catch and translate
  third-party exceptions into explicit status for the Core. Exceptions must
  never cross into Core code.
- Global `operator new/new[]` in Core are fatal on OOM (call `std::terminate`).
  Nothrow forms return `nullptr`. Core does not emit `std::bad_alloc`.

Build flags (Core targets):

- MSVC: `/EHs-` (no C++ EH), keep SEH as needed; do not use `/EHsc` in Core.
- Clang/GCC: `-fno-exceptions`.

Pattern:

```cpp
// Core API (status-based)
struct [[nodiscard]] Status
{
    bool ok;
    const char* msg; // optional, static or caller-managed
};

[[nodiscard]] inline Status LoadFoo(BufferView src, Foo& out) noexcept;
```

### 8.2 RTTI

Policy:

- Forbidden in Core: no `dynamic_cast`, no `typeid`.

Alternatives:

- Static polymorphism: concepts/CRTP/templates.
- Tiny dynamic facades: explicit V-tables (structs of function pointers).

Build flags (Core targets):

- MSVC: `/GR-`
- Clang/GCC: `-fno-rtti`

### 8.3 STL usage

Allowed with constraints:

- Public headers prefer views and POD (`std::span`, pointers+sizes, simple
  structs). Avoid exposing owning standard containers in public ABI.
- Internal implementation can use a curated subset.

Curated subset (typical):

- Fundamentals: `<array> <span> <bit> <type_traits> <limits> <utility> <tuple>
  <optional> <variant> <string_view>`
- Containers if needed and wired to the engine allocator via an adapter:
  `<vector> <deque> <string> <unordered_map> <unordered_set>`
- Avoid heavy subsystems unless justified: no `<regex>`, `<filesystem>`,
  `<iostream>`, `<locale>`, `<future>`, `<thread>` in Core by default.

Rules:

- No hidden allocations on hot paths.
- Do not introduce public API that implicitly requires exceptions/RTTI.

### 8.4 Namespaces and globals

- Everything lives under `namespace dng { ... }`.
- No anonymous namespaces in public headers.
- No mutable global state in headers.

### 8.5 Assertions and logging

- Programmer errors: `DNG_ASSERT(cond)`.
- Recoverable conditions: `DNG_CHECK(cond)` + return status.
- Heavy logging guarded by `Logger::IsEnabled("Category")`.

### 8.6 Policy lint gates (what CI enforces today)

The repository includes a lightweight policy linter (`tools/policy_lint.py`).
It currently scans `Source/Core/**` (and optionally `Source/Modules/**`) and
enforces these rules:

- Forbidden exception/RTTI tokens in Core: `throw`, `try`, `catch`,
  `dynamic_cast`, `typeid`.
- Raw `new`/`delete` expressions are forbidden in Core (placement-new is allowed).
- "Heavy" includes are forbidden unless explicitly allowlisted:
  - `<regex>`, `<filesystem>`, `<iostream>`, `<locale>`
- Code files must be **ASCII-only bytes** (no UTF-8 punctuation, no BOM).
- The linter also flags:
  - `using namespace` in Core code
  - `std::shared_ptr` / `std::weak_ptr` (discouraged)
  - raw `assert(...)` usage (prefer `DNG_ASSERT` / `DNG_CHECK`)
  - CRT alloc calls (`malloc/free/realloc/calloc`) outside blessed files

Blessed exceptions (narrow, documented):

- `Source/Core/Memory/GlobalNewDelete.cpp` may call `malloc/free` to avoid
  recursion when implementing global new/delete.

### 8.7 Suggested build profiles

- `core_debug`: `-O0`, asserts ON, logs ON, no exceptions/RTTI
- `core_release`: `-O3`, asserts OFF (or light), logs minimal, no exceptions/RTTI
- `interop_exceptions` (optional): isolated target with exceptions ON to wrap
  third-party libs; must translate to status before returning to Core

-------------------------------------------------------------------------------

## 9. Header-first strategy (fast build, thin facades)

Objective: contracts in self-contained headers while confining heavy
implementation details outside the public inclusion cone.

### 9.1 The "thin facade" pattern

- `Public.hpp`: declares only the contract (PODs, handles, spans, small inline).
- `detail/*.hpp`: heavy templates and implementation details, never included by
  other modules, only by the module's own `.cpp`.
- `detail/*.inl` (optional): inline chunks included from the module `.cpp` or a
  private TU, never from public headers.

### 9.2 Explicit instantiation

For templates with a finite set of combinations, use `extern template` to avoid
re-instantiation.

```cpp
namespace dng
{
    template<class T>
    void ProcessPodArray(Span<const T> in, Span<T> out) noexcept;

    // Prevent instantiation in consumer TUs
    extern template void ProcessPodArray<float>(Span<const float>, Span<float>) noexcept;
}
```

Implementation TU:

```cpp
#include "Public.hpp"
#include "detail/HeavyAlgo.hpp" // Heavy templates live here

namespace dng
{
    // Explicit instantiation: compiled once, linked everywhere
    template void ProcessPodArray<float>(Span<const float>, Span<float>) noexcept;
}
```

### 9.3 Build hygiene

- Self-contained headers: compile-only tests in `tests/SelfContain/`.
- IWYU: no transitive includes.
- PCH: lightweight, platform types and logging macros only.

### 9.4 Template vs v-table policy

- Internal templates in `detail/` for hot paths.
- Small public v-tables to stabilize headers and limit recompilation.
- ABI surface: prefer C ABI tables.

-------------------------------------------------------------------------------

## 10. Determinism policy

Purpose: define what determinism means in D-Engine and the rules required for
Replay-mode reproducibility.

### 10.1 Determinism levels

- Off: performance-first, no guarantees.
- Replay (default for debug/tests): same inputs + same build + same machine =>
  same simulation outputs.
- Strict (experimental): stronger reproducibility via extra restrictions.

### 10.2 Core guarantees (Replay)

In Replay mode:

1) Simulation time is derived only from SimulationClock.
2) RNG is explicit and deterministically seeded.
3) Results do not depend on OS time, thread timing, pointer addresses, unordered
   iteration, or nondeterministic job completion order.
4) Parallel simulation work follows the Threading Rules (private lanes +
   deterministic merge).

### 10.3 Stable ordering rules

If order affects results, it must be stable:

- iterate arrays/vectors with stable order,
- sort keys/handles/IDs before applying effects,
- merge per-entity/per-chunk buffers in ascending ID order.

Do not rely on hash container iteration order.

### 10.4 Verification

Minimum recommended test:

- run N simulation ticks,
- feed a deterministic CommandBuffer per tick,
- compute a stable hash of simulation state every M ticks,
- compare against a baseline.

### 10.5 Subsystem contract requirements (mandatory fields)

Every subsystem contract MUST explicitly declare:

- `SupportsReplayDeterminism`: yes/no
- `SupportsStrictDeterminism`: yes/no
- `DeterminismNotes`: known nondeterminism sources and mitigations
- `ThreadSafetyNotes`: what the caller must guarantee

Examples (conceptual):

- Audio: Replay determinism may guarantee event timeline stability, not
  sample-bitwise identical mixing.
- Renderer: visuals may be nondeterministic; renderer must not affect simulation
  state in Replay mode.
- IO/Streaming: must not feed nondeterministic data into simulation without
  recording.

-------------------------------------------------------------------------------

## 11. Threading rules (Replay determinism compatible)

Core principle:

In Replay mode, parallelism must be structured so the result does not depend on
execution timing and final effects are merged in deterministic order.

### 11.1 Allowed patterns

Pattern A: write to your own lane + deterministic merge

- each job writes to a private buffer (per-job/per-entity/per-chunk)
- merge applies results in stable order (ascending IDs)

Pattern B: stable reduction trees

- fixed partitioning and stable pairings
- beware float non-associativity

Pattern C: truly order-independent operations (e.g., integer bitset OR)

### 11.2 Forbidden patterns (Replay)

- multiple jobs writing directly to shared simulation state without deterministic
  ordering
- "who finishes first" logic
- parallel float reductions without stable ordering
- relying on thread ids, OS scheduling, address ordering, unordered iteration

### 11.3 Phase structure (recommended)

Per tick:

1) read-only gather (parallel OK)
2) compute phase writing private outputs (parallel OK)
3) deterministic merge/apply (single-thread default in Replay)
4) post-tick validation

### 11.4 Ownership and mutation rules

Simulation state is owned by systems or phases. Mutations must be controlled.

Rules:

1) Avoid shared mutable state between jobs.
2) If multiple jobs contribute to the same logical output, use a deterministic merge.
3) Do not use atomics as "gameplay logic ordering" in Replay mode.

### 11.5 Container and iteration rules

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

### 11.6 Scheduling rules (Replay)

In Replay mode:

- Job submission order must be stable.
- Partitioning must be stable (fixed chunk sizes or deterministic chunking rules).
- No auto-tuning that changes partitioning without being recorded.

Default recommendation:

- Replay simulation runs single-thread.
- Parallelism in Replay is only enabled when the engine provides a proven
  deterministic scheduler and merge framework.

### 11.7 Diagnostics

When possible, provide debug checks:

- detect direct shared-state writes from jobs in Replay mode
- assert stable ordering assumptions in merges
- optional tracing that records merge order for debugging

### 11.8 Rationale

These rules prevent two major classes of bugs:

1) Data races and nondeterministic ordering causing hard-to-repro issues.
2) Floating-point and ordering effects causing Replay drift across runs.

-------------------------------------------------------------------------------

## 12. Time policy (SimulationClock vs RealClock)

Two time sources:

### 12.1 SimulationClock (deterministic)

- used by simulation logic
- advances in fixed steps
- Replay mode: only allowed time source for simulation

Properties:

- TickIndex: monotonic integer tick counter
- FixedDeltaSeconds: constant step size (default 1/60)
- SimTimeSeconds: derived = TickIndex * FixedDeltaSeconds

### 12.2 RealClock (nondeterministic)

- used by tools, UI, profiling display, wall-clock timers
- derived from OS time, not deterministic
- must not drive simulation in Replay

### 12.3 Fixed-step simulation loop

- accumulator pattern
- SimulationStep depends only on SimulationClock + recorded inputs + explicit RNG
- rendering interpolates between previous/current simulation state

Timers/cooldowns should prefer ticks (EndTick = CurrentTick + DurationTicks).

### 12.4 Default fixed step

Recommended engine default:

- FixedDeltaSeconds = 1/60

Project-level configuration may choose 1/120 for high-demand action titles.
Engine policy: fixed-step is configurable; the existence of fixed-step is not
optional.

### 12.5 Input sampling policy (repro friendly)

For realtime action:

- Sample raw input as late as possible before each simulation tick.
- Convert raw input into a deterministic CommandBuffer for that tick.
- Simulation consumes CommandBuffer only.

In Replay mode:

- CommandBuffers are recorded or generated deterministically.
- Raw OS events are not used as a direct simulation input source.

### 12.6 Timers and cooldowns

Timers in simulation should be expressed in:

- ticks (preferred), or
- fixed-step time derived from ticks

Avoid:

- comparing against RealClock time
- fractional time accumulation that depends on variable dt

Recommended:

- store "end tick" for cooldowns: EndTick = CurrentTick + DurationTicks
- compute remaining time from tick difference if needed for UI

### 12.7 Editor and tools

The editor may use RealClock for UI and interaction.
When the editor drives simulation:

- it should advance SimulationClock deterministically (fixed-step)
- it should not inject RealClock into simulation decisions

### 12.8 Logging and profiling

- Profiling timestamps can use RealClock.
- Simulation logs should tag events with TickIndex for reproducibility.
- Avoid mixing wall time into simulation event ordering.

-------------------------------------------------------------------------------

## 13. Memory policy (defaults, precedence, invariants)

Purpose: production defaults for memory subsystem knobs and how runtime
precedence works for effective values.

Contract:

- Applies to Release | x64 unless overridden.
- Effective values are determined at startup by: API -> environment -> macros.
- Changes are logged once at `MemorySystem` initialization.

### 13.1 At a glance

- Effective defaults (Release | x64): tracking sampling=1, tracking shards=8,
  SmallObject batch=64
- Determinism-first: no hidden costs

### 13.2 Choosing the right allocator

| Usage pattern | Recommended allocator(s) | Rationale |
| --- | --- | --- |
| Temporary, transient work (default) | `SmallObjectAllocator` + `FrameScope` | Hot-path bump-style flow; `FrameScope` guarantees rewind at scope exit. |
| Persistent systems | `DefaultAllocator` or `PoolAllocator` | General-purpose fallback or fixed pools with predictable reuse. |
| High-safety diagnostics | `GuardAllocator`, `TrackingAllocator` | Higher cost but maximum visibility. |
| High alignment / large payloads | parent allocator | `SmallObjectAllocator` caps natural alignment; bypass for >= 32-byte alignment. |

### 13.3 Effective defaults (Release)

- Tracking sampling: 1 (`DNG_MEM_TRACKING_SAMPLING_RATE`)
- Tracking shards: 8 (`DNG_MEM_TRACKING_SHARDS`)
- SmallObject batch: 64 (`DNG_SOALLOC_BATCH`)

### 13.4 Precedence and observability

Resolution order:

1) API override via `dng::core::MemoryConfig` passed to `MemorySystem::Init()`
2) Environment variables at process start:
   - `DNG_MEM_TRACKING_SAMPLING_RATE`
   - `DNG_MEM_TRACKING_SHARDS`
   - `DNG_SOALLOC_BATCH`
3) Compile-time macros (see `Source/Core/Memory/MemoryConfig.hpp`)

MemorySystem logs the final effective values and their source once at init.

### 13.5 Constraints and clamping

- sampling >= 1; values > 1 currently clamp to 1
- shards must be power-of-two; invalid values fall back to macro default
- batch clamped to `[1, DNG_SOA_TLS_MAG_CAPACITY]`

### 13.6 Notes and rationale (why these defaults exist)

- Sampling > 1 is planned for future work, but until the accounting model and
  reporting format can represent sampled tracking safely, the engine clamps to 1.
- Sharding is used to reduce contention in tracking structures and reduce cache
  line bouncing.
- SmallObjectAllocator batching is a throughput knob: higher batch reduces calls
  to parent allocator but increases per-thread retained memory.

Rules of thumb:

- If you see contention in tracking, increase shards.
- If you see too much retained memory (TLS magazines), reduce batch.
- For deterministic CI, prefer sampling=1.

### 13.7 Engine invariants (must hold)

- Every allocation has a matching free with the same size and alignment.
- Alignment helpers must be used consistently:
  - `NormalizeAlignment`
  - `AlignUp`
  - `IsPowerOfTwo`
- Allocators must document:
  - thread-safety
  - ownership and lifetime
  - whether they can return nullptr
  - OOM behavior

-------------------------------------------------------------------------------

## 14. ABI and interop policy (stable C ABI for modules)

Purpose:

- define a stable, language-agnostic plugin boundary
- keep Core in C++ while enabling modules/backends in other languages
- make interop boring, explicit, deterministic-friendly, reviewable

Hard rules:

- the only official cross-language boundary is a C ABI
- no exceptions/RTTI in Core; absolutely no unwinding across ABI
- no hidden allocations at ABI boundary; ownership is explicit
- ABI v1 is frozen; incompatible evolution uses v2/v3 names

### 14.1 Terminology

- Core: engine runtime in C++
- Contract: C++ backend-agnostic interface (source-level stable)
- ABI contract: stable binary boundary (C ABI)
- Host: engine side that loads modules
- Module: dynamically loaded library implementing subsystem backends

### 14.2 The one true ABI shape

- function table + context (vtable-in-C)
- status codes, not exceptions
- POD-only data

### 14.3 ABI type rules (review rules)

- fixed-width integer types
- no `bool` in ABI (use `uint8_t`)
- strings are (ptr + len)
- arrays are (ptr + count)
- handles are integers, not pointers
- struct layout is stable; no implicit packing

### 14.4 Ownership and allocation

- "who allocates frees"
- prefer caller-allocated output (two-call pattern)
- host allocator services: `alloc(size, align)` and `free(ptr, size, align)`
- avoid hidden allocation in hot paths

### 14.5 Versioning

- v1 is immutable once published
- breaks create new names (`*_v2`, `dngModuleGetApi_v2`)
- if using struct extension, append fields only and validate `struct_size`

-------------------------------------------------------------------------------

## 15. ABI review checklist

If any item fails, the change is rejected or redesigned.

### A. Interface shape

- [A1] Cross-language boundary is C ABI only
- [A2] Function table + context pattern
- [A3] Functions return `dng_status_t` + out-params
- [A4] Explicit thread-safety notes
- [A5] Callbacks are `noexcept` and documented as non-throwing

### B. Unwinding and safety

- [B1] No exceptions/panics can escape
- [B2] Exceptions are caught and converted to status
- [B3] Callbacks documented to never unwind

### C. Data and layout

- [C1] POD-only, fixed-width primitives
- [C2] Enums explicit size
- [C3] Bool as `uint8_t`
- [C4] Strings (ptr + len)
- [C5] Arrays (ptr + count)
- [C6] Handles are integers
- [C7] Alignment/packing documented
- [C8] `sizeof/alignof` invariants checked
- [C9] Extensible structs use `struct_size` and validate it
- [C10] Reserved/padding fields are append-only

### D. Ownership and allocation

- [D1] Ownership explicit
- [D2] Matching free path or host allocator usage
- [D3] Hot paths avoid hidden allocations
- [D4] Scratch buffers/arenas used where appropriate

### E. Versioning

- [E1] No modifications to released v1 structs/signatures
- [E2] Breaking change introduces v2 names
- [E3] Append-only extension validates `struct_size`
- [E4] ABI version exposed and checked at load time

### F. Determinism

- [F1] Nondeterminism sources documented
- [F2] Deterministic modes described in caps/flags
- [F3] Ordering guarantees spelled out

### G. Tests

- [G1] Header self-contained test for new headers
- [G2] Compile-time layout check where relevant
- [G3] Smoke test loads module and calls minimal sequence
- [G4] ABI headers covered by `extern "C"` compile-only TU

-------------------------------------------------------------------------------

## 16. Benchmark protocol

### 16.1 Purpose

- Document how BenchRunner is executed in local gates and CI.
- Keep performance comparisons reproducible enough for regression detection.
- Describe the JSON payload emitted by the current benchmark harness.

### 16.2 Execution model

BenchRunner (`tests/BenchRunner/BenchRunner_main.cpp`) runs a fixed suite and exposes:

- `--warmup N`
- `--target-rsd P`
- `--max-repeat M`
- `--repeat M` (alias for `--max-repeat`)
- `--iterations K`
- `--cpu-info`
- `--strict-stability`

For each scenario, the runner:

1. executes warmup runs,
2. measures repeated batches,
3. computes `ns/op`,
4. stops early when `RSD <= target-rsd` or when `max-repeat` is reached,
5. marks the scenario as `unstable` when target RSD is still not met.

### 16.3 Affinity and priority

The runner itself does not currently pin affinity or set process priority.

Stabilization is applied by the caller in gates/CI:

- `tools/run_all_gates.ps1` launches BenchRunner via:
  - `cmd /c start /wait /affinity 1 /high ...`
- `.github/workflows/bench-ci.yml` uses the same pattern.

### 16.4 Recommended invocation

- Core compare run:
  - `x64\Release\D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 20 --cpu-info`
- Memory compare run:
  - `x64\Release\D-Engine-BenchRunner.exe --warmup 2 --target-rsd 8 --max-repeat 24 --cpu-info --memory-only --memory-matrix`

Related helpers:

- Local memory sweep helper:
  - `python tools/memory_bench_sweep.py --strict-stability --stabilize --compare-baseline`
- Baseline capture helper (safe default, no overwrite):
  - `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both`
- Baseline promotion helper (explicit overwrite):
  - `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both -Promote`

### 16.5 CI compare policy

- Core baseline:
  - `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`
- Memory baseline:
  - `bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json`

Notes:

- Core compare suppresses unstable-only noise for `baseline_loop`.
- Memory compare uses an 8% threshold (tracking and non-tracking paths) and
  allows unstable statuses when the baseline is already unstable.
- Current memory noise watchlist (ignored in compare):
  - `small_object_alloc_free_16b`
  - `small_object_alloc_free_small`

Bench profile knobs can be tuned without script edits:

- `BENCH_AFFINITY_MASK`, `BENCH_NORMAL_PRIORITY`
- `BENCH_CORE_WARMUP`, `BENCH_CORE_TARGET_RSD`, `BENCH_CORE_MAX_REPEAT`
- `BENCH_MEMORY_WARMUP`, `BENCH_MEMORY_TARGET_RSD`, `BENCH_MEMORY_MAX_REPEAT`

### 16.6 Leak gate

Bench and smoke logs are scanned for hard leak markers:

- `=== MEMORY LEAKS DETECTED ===`
- `TOTAL LEAKS:`

Presence of either marker fails the gate.

### 16.7 JSON output (schema v2)

BenchRunner writes `artifacts/bench/bench-<epoch-seconds>.bench.json`:

```json
{
  "schemaVersion": 2,
  "benchmarks": [
    {
      "name": "baseline_loop",
      "value": 2.085922,
      "rsdPct": 5.424463,
      "bytesPerOp": 0.0,
      "allocsPerOp": 0.0,
      "status": "ok",
      "reason": "",
      "repeatsUsed": 4,
      "targetRsdPct": 3.0
    }
  ],
  "summary": {
    "okCount": 10,
    "skippedCount": 1,
    "unstableCount": 0,
    "errorCount": 0
  },
  "metadata": {
    "note": "BenchRunner v2",
    "strictStability": true,
    "schemaVersion": 2,
    "unit": "ns/op"
  }
}
```

Notes:

- `value` is measured `ns/op`.
- `bytesPerOp` and `allocsPerOp` are measured at runtime through `Core/Diagnostics/Bench.hpp`.
- `status` and `reason` make skipped/unavailable scenarios explicit.

-------------------------------------------------------------------------------

## 17. Roadmap (two tracks: SDK stability + crowd slice)

The roadmap is split into two tracks:

- Track A: engine foundation (contracts, ABI, determinism, memory, runtime)
- Track B: crowd-first vertical slice (the proof)

The point of Track B is to create a concrete public demonstration while Track A
keeps the architecture honest.

### 17.1 Track A - Foundation

Phase A0 (done): Contract SDK stabilization

- contracts and ABI shape established
- Null backends + systems + smoke coverage
- policy lint + header self-containment gates
- bench runner + baselines

Phase A1 (next): Core runtime solidity

- runtime orchestration (init/shutdown) remains clean and rollback-safe
- memory system continues to harden (tracking, OOM policy, reporting)
- deterministic job mode (Replay-friendly) with instrumentation
- ABI loader hardening and packaging of SDK

Phase A2: Platform-ready renderer and windowing (Windows)

- choose renderer strategy (DX12 recommended but not mandatory)
- Win32 windowing + swapchain + input path
- GPU resource lifetime rules and frame submission contract

### 17.2 Track B - Crowd-first vertical slice

Milestone B0: Deterministic crowd simulation harness

- headless simulation benchmark: update N agents per tick
- stable ordering, explicit RNG, fixed-step
- state hash baseline (Replay test)

Milestone B1: Animation sampling and submission

- skeletal pose sampling for N agents
- memory and job partitioning rules
- stable merge and determinism notes

Milestone B2: Minimal renderer path

- enough rendering to visualize the crowd
- stable frame pacing and frame-time measurement

Milestone B3: VFX and interaction stress

- bursts (impacts), simple collisions, VFX submissions
- demonstrate frame-time stability under stress

Deliverable: a documented, reproducible benchmark scene and a report.

-------------------------------------------------------------------------------

## 18. Implementation snapshot (code-backed reality)

This section is derived from the repository's code layout and smoke targets.

Current architecture:

- Core pattern per subsystem:
  - Contract: `Source/Core/Contracts/<Subsystem>.hpp`
  - Null backend: `Source/Core/<Subsystem>/Null<Subsystem>.hpp`
  - Orchestrator: `Source/Core/<Subsystem>/<Subsystem>System.hpp`
- Runtime orchestration:
  - `Source/Core/Runtime/CoreRuntime.hpp`

Test/target reality:

- `tests/AllSmokes/AllSmokes_main.cpp` -> `AllSmokes.exe`
- `tests/MemoryStressSmokes/MemoryStressSmokes_main.cpp` -> `MemoryStressSmokes.exe`
- `tests/Abi/ModuleSmoke.cpp` -> `ModuleSmoke.exe`
- `tests/BenchRunner/...` -> `D-Engine-BenchRunner.exe`
- header self-containment: `tests/SelfContain/`
- policy tests: `tests/Policy/`

If this section becomes wrong, it must be updated when code changes.

-------------------------------------------------------------------------------

## 19. Contribution workflow and review checklist

### 19.1 Workflow (practical)

1) Extend/add a contract in `Source/Core/Contracts/`.
2) Implement/update Null backend and system layer.
3) Wire runtime integration in `Source/Core/Runtime/CoreRuntime.hpp` when needed.
4) Add smoke coverage in `tests/Smoke/...` and aggregator wiring if runnable
   behavior changed.
5) Add/adjust header self-containment tests.
6) Run policy lint and gates.
7) Update this handbook if any policy/roadmap/architecture rule changes.

### 19.2 Review checklist (engine-wide)

Documentation coherence:

- this handbook remains the canonical source
- new docs do not duplicate policies; they link here

Code coherence:

- contracts live in `Source/Core/Contracts/`
- every subsystem has contract + null backend + system + tests
- no exceptions and no RTTI in Core
- no hidden allocations across contract boundaries
- headers are self-contained (self-contain tests green)

Tooling coherence:

- gates and bench CI match benchmark protocol
- policy lint remains strict

-------------------------------------------------------------------------------

## 20. Appendix: historical snapshots and where to look next

If you are new:

- start at `README.md` (build quickstart)
- read this handbook
- inspect a contract header in `Source/Core/Contracts/`

Historical milestone snapshots (may be stale):

- `Docs/Progress_Summary_v0.1.md`
- `Docs/*_M0_Status.md`

ABI author guide:

- see `Docs/ABI_Module_Authoring.md` (pointer) and this handbook's ABI section.
