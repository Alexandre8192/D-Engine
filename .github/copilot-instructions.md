# D-Engine - GitHub Copilot Instructions (v3)

You are a code reviewer and generator for D-Engine.
Primary objective: performance-first with contracts-first modularity and auditable code.
When in doubt, favor: clarity, full documentation, zero hidden costs.

D-Engine is a programmer-first engine, not a general app. Core code must stay predictable, explicit, and easy to reason about.

## 0) Golden Rules (do not break)

Core language / structure
- Header-first C++23/26; `.cpp` only if technically required (global new/delete, platform linkers, thin translation units).
- Headers must be self-contained; no hidden includes; use engine-absolute includes only.
- No exceptions and no RTTI in Core. Do not introduce `throw`, `dynamic_cast`, or `typeid`.
- Do not introduce raw `new` / `delete` in Core. All allocations go through D-Engine allocators.

Ownership and pointers
- Raw pointers in Core are non-owning views by default. If you introduce a raw pointer, document it as a view (no ownership, no lifetime extension).
- For ownership in Core, prefer value types, handles (indices + generation), or engine-specific RAII wrappers bound to an allocator.
- Do not use `std::shared_ptr` or `std::weak_ptr` in Core. `std::unique_ptr` is allowed only in tools/tests and non-hot paths, never in the hot Core API surface.

Namespace / macros / attributes
- Language policy: use `dng::` namespace and `DNG_` macros.
- Apply `[[nodiscard]]`, `constexpr`, and `noexcept` by default when sensible; enforce invariants with `static_assert`.
- Keep comments and identifiers ASCII-only.

Memory discipline
- All allocations must go through D-Engine allocators; `free` must match allocation size and alignment.
- Reuse `NormalizeAlignment`, `AlignUp`, `IsPowerOfTwo` from `Alignment.hpp`. Do not reimplement alignment helpers.
- No hidden allocations in hot paths (containers, virtual calls, or lambdas that allocate must be clearly documented or avoided).

Diagnostics
- Guard risky code with `DNG_CHECK` / `DNG_ASSERT`. Use `DNG_CHECK` for recoverable issues and `DNG_ASSERT` for must-hold invariants.
- Keep heavy logging behind `Logger::IsEnabled()` checks; avoid building log strings when the logger is disabled.

Dependencies
- Do not add new STL subsystems or external libraries without a Design Note justifying performance, portability, and compile-time impact.
- Avoid heavy STL components (`<regex>`, `<filesystem>`, `<iostream>`, `<locale>`) unless explicitly justified.

Tests and docs
- Every public symbol must have a Purpose / Contract / Notes block.
- For each new header, add a compile-only smoke test in `tests/` that includes it from a fresh translation unit.
- Keep documentation and implementation in sync. If you change behavior, update Purpose / Contract / Notes.

Tooling and safety
- Code must compile warning-clean at high warning levels (e.g. `/W4` or `-Wall -Wextra`) with warnings treated as errors in CI.
- New code should be friendly to sanitizers (ASan, UBSan) and static analyzers (clang-tidy). Avoid undefined behavior and rely on well-defined constructs.

## 1) Performance-First Mandate

- Prefer data-oriented layouts, predictable branching, and deterministic hot paths.
- Any hot path must have its allocation and branch behavior understood and, if non-trivial, documented in Notes.
- No hidden allocations in hot loops. If an allocation is unavoidable, provide a toggle or a pre-allocation path and document the cost.
- Bench-sensitive changes should include a small `DNG_BENCH` snippet or be wired into the existing bench harness JSON output.
- Avoid virtual dispatch and type-erased abstractions on hot paths unless you can justify them clearly in Notes with a cost model.

## 2) Contracts-First Modularity

- New systems (Renderer / Geometry / Visibility / Audio / IO / etc.) start with a contract in `Source/Core/Contracts/`.
- Provide both a static face (concepts / CRTP) and a dynamic face (tiny v-table) behind a single public header when polymorphism is required.
- Declare capability flags for optional features (mesh shaders, bindless, ray tracing, etc.) and provide documented fallbacks.
- Always ship a Null backend plus a minimal example backend with smoke tests that exercise the contract.

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
````

For each public function, add a short in-function block:

```cpp
// Purpose : <1 line>
// Contract: <pre/post; complexity; allocations; ownership>
// Notes   : <non-obvious choices, caveats, or links to docs>
```

Keep comments in English and ASCII-only.

## 4) Review Checklist (must pass)

Compilation / structure

* [ ] Header compiles alone (self-contained).
* [ ] Includes use engine-absolute paths; no hidden transitive dependencies.
* [ ] `.cpp` files added only when technically required and the reason is documented.

API and documentation

* [ ] Purpose / Contract / Notes present for public types and functions.
* [ ] Thread-safety and allocation behavior are explicit in the Contract.
* [ ] `[[nodiscard]]` / `constexpr` / `noexcept` applied where appropriate.
* [ ] `static_assert` guards invariants (sizes, alignments, enum ranges, assumptions).

Memory and diagnostics

* [ ] All allocations go through engine allocators; frees match size and alignment.
* [ ] Alignment helpers are reused from `Alignment.hpp`.
* [ ] `DNG_CHECK` / `DNG_ASSERT` protect risky operations and assumptions.
* [ ] Logging cost is gated behind `Logger::IsEnabled()`; no heavy formatting when disabled.

Performance and determinism

* [ ] No hidden allocations in hot paths (including STL containers growing unexpectedly).
* [ ] Data layout (AoS vs SoA) is chosen and briefly justified for hot structures.
* [ ] Capability flags gate optional feature paths; fallbacks exist and are documented.
* [ ] No unnecessary virtual dispatch or type-erasure in hot paths.

Pointers and ownership

* [ ] Raw pointers are clearly documented as non-owning views.
* [ ] Ownership is expressed via values, handles, or engine RAII wrappers, not ad-hoc raw pointers.
* [ ] No `std::shared_ptr` / `std::weak_ptr` introduced in Core.
* [ ] Any use of `std::unique_ptr` is limited to tools/tests or cold paths and does not hide allocations on hot paths.

Testing and bench

* [ ] Compile-only smoke test added for new headers.
* [ ] If perf-sensitive: bench snippet or harness wiring added with at least one tracked metric.
* [ ] New behavior has at least one test (unit or integration) when reasonable.

Dependencies

* [ ] No new dependencies added; or a Design Note explains why they are needed and what they cost.

Tooling and safety

* [ ] Code is compatible with high warning levels and treats warnings as errors in CI.
* [ ] New patterns do not rely on undefined behavior; they should be friendly to ASan / UBSan.
* [ ] Code does not defeat static analysis (avoid complex macros hiding control flow).

## 5) Patterns to Prefer

Memory and ownership

* Use `AllocatorRef::New<T>` / `NewArray<T>` (or the relevant engine allocator helpers) for owned allocations; normalize alignment first.
* Represent ownership using:

  * values (POD structs, small aggregates),
  * handles (index + generation),
  * or engine-specific RAII wrappers that know which allocator to free from.
* Use raw pointers, spans, or views only for non-owning access. Document that they do not extend lifetime.

Assertions and logging

* Use `DNG_CHECK` for recoverable conditions (e.g. bad user input).
* Use `DNG_ASSERT` for programming errors that must never happen under a valid contract.
* Wrap logging in `Logger::IsEnabled()` checks. Avoid building `FString`-like or `std::string` temporaries when logs are disabled.

Compile-time validation

* Use `static_assert(std::is_trivially_copyable_v<T>)` or similar checks to enforce layouts.
* Use concepts or type traits to constrain template parameters and catch misuse at compile time.

API surface and types

* Prefer POD views for public APIs (slices, spans, simple structs).
* Use `enum class` rather than plain enums for new enumerations.
* Avoid implicit conversions. Prefer explicit constructors and helper functions when in doubt.
* Consider strong typedefs (aliases or wrapper structs) for quantities like durations, sizes, or ids to avoid mixing units accidentally.

Structure and abstraction

* Avoid global mutable state in headers. Pass contexts explicitly.
* Prefer free functions and small POD structs to deep inheritance hierarchies.
* Use concepts/CRTP by default for static polymorphism; use tiny virtual interfaces only when necessary and justified.

## 6) Rendering / Geometry (guidance)

* Default path should be classic GPU-driven rendering: visibility buffer, occlusion (Hi-Z / MOC), and indirect draws (MDI).
* Optional modules may implement virtual geometry or clustered visibility. They must document limitations (transparency, skinning, LOD rules).
* Materials should prefer bindless or material tables when hardware capabilities allow, with a clear documented fallback.
* Public APIs should expose only views (no ownership) across renderer/geometry boundaries.

## 7) Anti-Patterns (refuse or flag)

Core and language

* Introducing exceptions, RTTI, or `new` / `delete` in Core.
* Adding `std::shared_ptr` or `std::weak_ptr` in Core, especially in hot paths.
* Using raw pointers for ownership without explicit documentation and RAII.

Memory and performance

* Hidden allocations in hot paths (STL growth, heap inside helper functions, dynamic polymorphism without a cost explanation).
* Re-implementing alignment or low-level math helpers instead of using `Alignment.hpp` and existing math utilities.
* Large or complicated constructors that allocate or perform I/O.

APIs and structure

* Heavy STL subsystems (`<regex>`, `<filesystem>`, `<iostream>`, `<locale>`) without a Design Note.
* Opaque macros that hide control flow or ownership; prefer small inline functions and clear templates.
* Magic numbers sprinkled in code. Use named `constexpr` values with a one-line rationale.

Tooling

* Code that compiles only with lax warning settings or relies on undefined behavior.
* Patterns that defeat static analysis or sanitizers (for example, deliberate UB tricks without a clear, documented justification in Notes).

## 8) When Proposing Code

When you generate or refactor code, always:

* Prefer explicit templates and clear ownership semantics.
* Document Purpose / Contract / Notes alongside every public addition or behavior change.
* Keep Core free of exceptions, RTTI, and raw `new` / `delete`.
* Express non-owning relationships with pointers or spans; express ownership with values, handles, or RAII.
* Add a smoke test showing a clean include from a fresh translation unit in `tests/`.
* For performance-sensitive changes, add a small bench snippet or wire into the bench harness.

## 9) Pull Request Template (for Copilot)

Summary: what and why in 1–3 sentences.

Compliance:

* [ ] Header compiles standalone.
* [ ] Purpose / Contract / Notes present and up to date.
* [ ] `[[nodiscard]]` / `constexpr` / `noexcept` applied where appropriate.
* [ ] Alignment and allocator rules honored; no raw `new` / `delete` in Core.
* [ ] Ownership is explicit (values, handles, RAII) and raw pointers are non-owning views.
* [ ] `DNG_CHECK` / `DNG_ASSERT` guard risky paths and invariants.
* [ ] No hidden allocations in hot paths.
* [ ] No new dependencies (or justified with a Design Note).
* [ ] Smoke test added for new headers.
* [ ] Bench snippet or harness wiring added for perf-sensitive changes.
* [ ] Code is compatible with high warning levels and friendly to sanitizers / static analyzers.

Design Notes:

* Document non-obvious choices, alternatives considered, ABI invariants, determinism requirements, and expected performance costs.

## 10) Memory Core Policy (summary)

- Core/Memory never uses raw `new` / `delete`. All allocations go through D-Engine allocators.
- Debug helpers in Core (guards, marker stacks, etc.) must also follow this rule:
  - either use fixed-capacity buffers with no dynamic allocation, or
  - allocate through a D-Engine allocator (e.g. a debug/guard allocator),
  but never call `new` / `delete` directly.
- Test/bench/tool code outside Core is allowed to use STL containers, smart pointers, and raw `new` / `delete` when convenient.
- Ownership is always explicit:
  - values, handles, or engine RAII wrappers for owning relationships,
  - raw pointers and views for non-owning access only.
- All allocators document their allocation, deallocation, and alignment behavior in Purpose / Contract / Notes blocks.
