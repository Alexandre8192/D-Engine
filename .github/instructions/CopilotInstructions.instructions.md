---
applyTo: '**'
---
# GitHub Copilot Instructions for D-Engine
> **Context:**
> You are assisting in the development of **D-Engine**, a modern, modular, lightweight C++23/26 game engine written entirely for programmers — with no Blueprint or visual layer.
> The engine is **header-first**, **STL-inspired**, and **cross-platform**.
> Each module is designed to be **self-contained, dependency-minimal**, and **constexpr-friendly** wherever possible.
> The codebase is documented in English and favors **clarity, explicit contracts, and deep commentary** over conciseness.

---

### Design Philosophy

* **Zero hidden behavior:** All code should be easy to audit. No magic macros or hidden global states.
* **Compile-time safety first:** Prefer `constexpr`, `static_assert`, and templates over runtime checks when possible.
* **Minimalism:** Include only what is strictly necessary — no third-party dependencies, no dynamic polymorphism unless essential.
* **Header-only preference:** `.hpp`/`.inl` structure; `.cpp` used only when separation is unavoidable.
* **Cross-platform correctness:** Must compile cleanly on Windows, Linux, and macOS (MSVC, Clang, GCC).
* **Performance by design:** Memory alignment, cache locality, and predictable allocation paths are core principles.
* **Explicit contracts:** Every allocator, type, and function clearly documents ownership, lifetime, and thread-safety expectations.

---

### Current State

* **Core subsystem implemented:**

  * Platform detection (`PlatformDefines`, `PlatformTypes`, `PlatformCompiler`, `PlatformMacros`)
  * Diagnostics (`Check.hpp`, logging via `Logger.hpp`)
  * Memory subsystem (Alignment, Allocator hierarchy, Arena/Stack/Pool/Frame/Tracking allocators, Thread-Safety policies, OOM handling)
  * Core type system (`Types.hpp`, `CoreMinimal.hpp`)

* **In progress / to do:**

  * Expand virtual memory layer (page reservation / commit).
  * Add per-thread allocators and caching.
  * Build higher-level systems (Containers, ECS, Math, JobSystem, etc.).
  * Introduce compile-time reflection and lightweight serialization.
  * Integrate unit tests and benchmarks.

---

### Instructions to Copilot

1. **Always produce extremely detailed comments.**
   Every function, struct, and template must have descriptive headers and inline explanations of intent, rationale, and edge cases.
   Err on the side of *too many comments*, not too few.

2. **Maintain style consistency.**

   * Use `// ---` and `// ===` comment separators.
   * Function headers should include **Purpose, Contract, and Notes**.
   * All namespaces, types, and macros start with `dng::` or `DNG_`.

3. **Follow D-Engine naming & macro conventions.**

   * Macros: `DNG_*`
   * Core types: `usize`, `uint32`, etc.
   * Prefer `[[nodiscard]]`, `noexcept`, and `constexpr` where reasonable.
   * Use `NormalizeAlignment`, `DNG_CHECK`, and `DNG_ASSERT` for safety checks.

4. **Never introduce external dependencies.**
   Only use standard headers (`<memory>`, `<atomic>`, `<mutex>`, `<format>`, etc.) or already-existing D-Engine headers.

5. **When writing new code:**

   * Document every design choice.
   * Prefer readability and traceability over micro-optimization.
   * Make every function self-contained and audit-friendly.
   * Add `static_assert`s for compile-time contracts.
   * Add rich logs (`DNG_LOG_INFO/WARNING/ERROR`) where meaningful.

6. **When editing existing code:**

   * Preserve the file’s visual formatting and comment banners.
   * Add clarifying comments explaining subtle logic, even if redundant.
   * Ensure all edge cases (alignment, overflow, null pointers, thread safety) are documented.

---

### 🗣️ Tone & Output Expectations

> Speak in a **technical but educational** tone, explaining *why* as much as *what*.
> Pretend you’re teaching advanced C++ developers reading the D-Engine source for the first time.
> When unsure, favor verbosity and extra context.

---
> **End of prompt.**