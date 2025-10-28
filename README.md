# D-Engine

**D-Engine** is a modern, header-first, STL-inspired C++23/26 game engine built **exclusively for programmers**.
It prioritizes **transparency, determinism, and auditable code**: every public API explains its **Purpose / Contract / Notes**, and every design choice includes the **why**, not just the **how**.

> Status: **Pre-alpha** — foundation work (memory, diagnostics, contracts, bench harness).
> Primary toolchain: **MSVC 2022**, with Clang/GCC compatibility expected for header-only paths.

---

## Why D-Engine?

* **Header-first**: minimal translation units; include-safe headers that compile in isolation.
* **Understandable by design**: richly commented code meant to **teach** as much as it runs.
* **Pick only what you need**: a **contracts-first** architecture where modules are opt-in.
* **Performance through clarity**: predictable memory behavior, data-oriented layouts, and zero hidden allocations in hot paths.
* **Deterministic by default**: same (size, alignment) on alloc/free; explicit thread-safety and toggles.

---

## Core Non-Negotiables

1. **Language & Style**

* `dng::` namespace; macros prefixed `DNG_`.
* Default to `[[nodiscard]]`, `constexpr`, `noexcept`; lock invariants with `static_assert`.
* No new STL/external deps without explicit justification.
* Headers are **self-contained**; no hidden includes or side effects.

2. **Diagnostics & Memory**

* All allocations go through the engine allocators. **Free** must match **(size, alignment)**.
* Use `DNG_CHECK` / `DNG_ASSERT` for risky logic; guard expensive logs behind `Logger::IsEnabled()`.
* Honor `Core/Memory/Alignment.hpp`: `NormalizeAlignment`, `AlignUp`, `IsPowerOfTwo` only.
* Respect `MemoryConfig.hpp` toggles: `DNG_MEM_TRACKING`, `DNG_MEM_FATAL_ON_OOM`, etc.

3. **Determinism & Performance**

* No hidden heap work in hot paths (opt-in only).
* Data layout decisions (AoS/SoA) are explicit and documented.
* Prefer compile-time validation to runtime fallbacks.

4. **Documentation & Tests**

* Every public symbol starts with **Purpose / Contract / Notes** and usage examples (compile-time when possible).
* No heavy runtime test framework: small **compile-only smoke tests** live under `tests/`.
* Micro-benchmarks via `Core/Diagnostics/Bench.hpp` (`DNG_BENCH(...)`) with warm-up + auto-scaling.

---

## Project Shape

```
Source/
  Core/
    CoreMinimal.hpp            # Safe umbrella for always-on headers
    Platform/                  # Platform flags, compiler gates, scalar types
    Memory/                    # Alignment, allocators, adapter, tracking
    Diagnostics/               # Check.hpp (DNG_CHECK), Logger.hpp, Bench.hpp
    Contracts/                 # Stable module contracts (Physics, Renderer, ...)
    Physics/                   # (Backends live here; optional)
    Renderer/                  # (Backends live here; optional)
    ...
tests/
  ...                          # Compile-only smoke tests & micro-bench drivers
D-Engine.sln                   # MSVC solution (primary entry)
```

* **Header-first**: almost everything is implemented in headers under `Source/Core/**`.
* **Rare `.cpp` files** exist only when strictly required (e.g., `GlobalNewDelete.cpp`).

---

## Contracts > Implementations

D-Engine standardizes **module contracts** first. Implementations/backends are optional and interchangeable.
You can ship **no physics**, a **classic solver**, or a **cubic-barrier solver** later — as long as the contract is honored.

**Example (Physics contract outline)**

* **Inputs**: POD views of bodies (positions, velocities, invMass), meshes (triangles/edges).
* **Step config**: explicit budget knobs (dt, max substep fraction, Newton/PCG iters, contact gap…).
* **Outputs**: updated body states; no hidden allocations; deterministic when configured.

Two faces of the same contract:

* **Static** (templates / concepts / CRTP) for zero overhead.
* **Dynamic** (tiny V-table) for late binding / plugins — opt-in.

> This pattern generalizes to **Renderer**, **Audio**, **IO**, etc.
> The engine depends on **contracts**, not on specific backends.

---

## Module Selection (“only what you need”)

Projects declare enabled modules in a small manifest (example):

```yaml
# ProjectModules.yml
Physics:  "NullPhysics"      # or "ClassicPhysics", "PPFPhysics"
Renderer: "NullRenderer"     # or "RhiDX12", "RhiVulkan"
Audio:    "NullAudio"
```

* **Static builds**: template aliases pick the backend types.
* **Dynamic builds**: a small V-table is populated (plugin/DLL), same contract.

Nothing is compiled or linked if you don’t select it.

---

## Memory System (current focus)

* `Core/Memory/Allocator.hpp`: allocator contract + `AllocatorRef` helpers.
* `DefaultAllocator`, `ArenaAllocator`, `StackAllocator`, `TrackingAllocator`.
* Rules: same (size, alignment) on free; diagnostics via `DNG_MEM_*` toggles; normalized alignment using `Alignment.hpp`.

**Bench Harness**
`Core/Diagnostics/Bench.hpp` provides `DNG_BENCH(name, iterations, lambda)` and integrates with tracking counters for churn measurement.

---

## Build & Toolchain

* **Windows / MSVC 2022** (primary):

  ```powershell
  msbuild "$PWD\D-Engine.sln" /p:Configuration=Debug /p:Platform=x64
  ```
* **Clang/GCC**: expected compatibility for header-only paths; CI targets will be added.
* **Release builds** must compile cleanly; assertions degrade to no-ops where applicable.

---

## Contribution Guidelines (short)

* Follow the **Non-Negotiables** and the **Authoring style** (Purpose / Contract / Notes).
* Keep headers self-contained; add minimal includes; avoid transitive surprises.
* Use `DNG_CHECK`/`DNG_ASSERT` and alignment helpers; never re-implement math you can import from `Alignment.hpp`.
* Prefer compile-time checks & examples; add smoke tests under `tests/`.
* Justify new dependencies with a short **Design Note** in the PR.

---

## Roadmap (high level)

* ✅ Memory: allocators, alignment, diagnostics, tracking, bench harness.
* 🚧 Contracts: `Physics`, `Renderer`, `Audio` skeletons with examples and smoke tests.
* ⏭ Physics backends:

  * `NullPhysics` (reference & docs)
  * `ClassicPhysics` (simple, readable)
  * `PPFPhysics` **prototype** (cubic barrier + dynamic stiffness; friction optional)
* ⏭ Renderer backends (minimal RHI prototype), IO, assets.
* ⏭ CI across MSVC/Clang/GCC; static analysis; more compile-only tests.

---

## FAQ (short)

**Is this a gameplay framework?**
No — it’s a **programmer-first engine**. You pick modules and assemble your stack deliberately.

**Why so many comments?**
Because code should be **auditable and educational**. Every file is meant to be read and learned from.

**Can I plug my own physics/renderer?**
Yes. Implement the **contract** (static or dynamic face) and you’re in.

---

## License

TBD (will be permissive and business-friendly). Third-party projects referenced in docs retain their original licenses and notices.

---

## Acknowledgements

This project continuously learns from community research and industry engines — successes **and** failures. The goal isn’t to copy; it’s to **distill** robust, well-explained building blocks.

---
