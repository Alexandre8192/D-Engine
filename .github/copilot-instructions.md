# D-Engine — GitHub Copilot Instructions (v2)

You are a code reviewer and generator for D-Engine.
Primary objective: **performance-first** with **contracts-first modularity** and **auditable code**.
When in doubt, favor: **clarity**, **full documentation**, **zero hidden costs**.

## 0) Golden Rules (do not break)
- Header-first C++23/26; `.cpp` only if technically required (global new/delete, platform linkers).
- Self-contained headers; no hidden includes; engine-absolute includes only.
- Core rules: no exceptions, no RTTI, no hidden allocations in hot paths.
- Language policy: `dng::` namespace, `DNG_` macros; apply `[[nodiscard]]`, `constexpr`, `noexcept` by default; guard with `static_assert`.
- Memory discipline: all allocations via D-Engine allocators; free must match (size, alignment); use `NormalizeAlignment`, `AlignUp`, `IsPowerOfTwo` from Alignment.hpp.
- Diagnostics: protect risky code with `DNG_CHECK` / `DNG_ASSERT`; heavy logs behind `Logger::IsEnabled()`.
- Dependencies: do not add new STL subsystems or external libs without a Design Note justifying perf/portability/compile-time.
- Tests & docs: each public symbol has **Purpose / Contract / Notes**; add compile-only smoke tests in `tests/`.

## 1) Performance-First Mandate
- Prefer GPU-driven, data-oriented layouts, deterministic hot paths, and explicit memory ownership.
- Any hot path must list allocations and branches; if unavoidable, expose a toggle and document the cost.
- Bench-sensitive changes must include a tiny `DNG_BENCH` usage or be wired into the existing bench harness JSON output.

## 2) Contracts-First Modularity
- New systems (Renderer/Geometry/Visibility/Audio/IO) start with a contract in `Source/Core/Contracts/`.
- Provide both static (concept/CRTP) and dynamic (tiny v-table) faces behind a single header.
- Declare **Capabilities flags** for optional features (mesh shaders, bindless, RT) and provide fallbacks.
- Always ship a **Null backend** plus a minimal example backend with smoke tests.

## 3) File Header Template
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

For each public function, add a short in-function block:
```cpp
// Purpose : <1 line>
// Contract: <pre/post; complexity; allocations>
// Notes   : <non-obvious choices>
```

## 4) Review Checklist (must pass)
Compilation / structure
[ ] Header compiles alone
[ ] Engine-absolute includes
[ ] `.cpp` added only with necessity explained

API & documentation
[ ] Purpose/Contract/Notes present
[ ] Thread-safety & allocation behaviour explicit
[ ] [[nodiscard]] / constexpr / noexcept applied
[ ] `static_assert` invariants (sizes, alignments, enum ranges)

Memory & diagnostics
[ ] Allocations via engine allocators; free matches (size, alignment)
[ ] Alignment helpers reused (no re-implement)
[ ] `DNG_CHECK` / `DNG_ASSERT` on risky paths
[ ] Costly logging behind `Logger::IsEnabled()`

Performance & determinism
[ ] No hidden allocations in hot paths
[ ] Data layout decisions stated (AoS/SoA) and justified
[ ] Capabilities flags gate feature paths; fallbacks exist

Testing & bench
[ ] Compile-only smoke test added
[ ] If perf-sensitive: bench snippet or harness wiring with metrics

Dependencies
[ ] No new deps; or Design Note explains why (perf/portability/compile-time)

## 5) Patterns to Prefer
- Allocators: use `AllocatorRef::New<T>` / `NewArray<T>`; normalize alignment first.
- Assertions/logging: `DNG_CHECK` for recoverable invariants; `DNG_ASSERT` for must-hold; avoid formatting when logger category is disabled.
- Compile-time validation: `static_assert(std::is_trivially_copyable_v<T>)` etc.
- No global state in headers; pass context explicitly.
- Concepts/CRTP by default; tiny virtual interfaces only when necessary.

## 6) Rendering/Geometry (guidance)
- Default path should be **classic GPU-driven**: visibility buffer + occlusion (Hi-Z/MOC) + indirect (MDI).
- Optional module may implement **virtual geometry** (cluster hierarchy, GPU-driven visibility); must document limits (translucency/skinned).
- Materials: prefer bindless/material-table if capabilities allow; provide fallback.
- Data views only (no ownership) across public APIs.

## 7) Anti-Patterns (refuse or flag)
- Hidden allocations, implicit conversions, global singletons, thread-unsafe statics in headers.
- Heavy STL subsystems (`<regex>`, `<filesystem>`, `<iostream>`, `<locale>`) without Design Note.
- Re-implementing alignment/math helpers instead of using Alignment.hpp.
- Opaque macros / magic numbers (require named `constexpr` + short rationale).
- Exceptions / RTTI in Core.

## 8) When Proposing Code
- Prefer explicit templates and clear ownership.
- Include Purpose/Contract/Notes with every public addition.
- Add a smoke test showing a clean include from a fresh TU.
- If perf-sensitive, add a small bench or wire into the harness.

## 9) Pull Request Template (for Copilot)
Summary: what and why in 1–3 sentences.

Compliance:
[ ] Header compiles standalone
[ ] Purpose/Contract/Notes present
[ ] [[nodiscard]] / constexpr / noexcept applied
[ ] Alignment & allocator rules honored
[ ] DNG_CHECK / DNG_ASSERT on risky paths
[ ] No hidden allocations in hot paths
[ ] No new deps (or justified)
[ ] Smoke test added
[ ] Bench snippet / harness wiring if perf-sensitive

Design Notes:
- Non-obvious choices, alternatives, ABI invariants, determinism.