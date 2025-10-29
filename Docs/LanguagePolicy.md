# `docs/LanguagePolicy.md`

````markdown
# D-Engine — Language Policy (Core)

> **Goal:** Deterministic, auditable, portable C++23/26 with zero hidden costs.
> This document defines **what the Core allows** and **why**. Subsystems and
> backends must conform.

## TL;DR

- **No exceptions in Core.** No `throw`, no `try/catch`. Use explicit status.
- **No RTTI in Core.** No `dynamic_cast`, no `typeid`.
- **STL allowed with constraints.** Public APIs expose views/POD; if a standard
  container is used internally, wire it to the engine allocator and avoid hidden
  allocations in hot paths.
- **Namespaces are fine.** Everything under `dng::`; no anonymous namespaces in
  public headers.
- **Assert-first.** Use `DNG_CHECK` / `DNG_ASSERT`; guard costly logs behind
  `Logger::IsEnabled()`.

---

## 1) Exceptions

### Policy
- **Forbidden in `Source/Core/**`** (engine core): **no `throw`, no `try/catch`**.
- Allowed **only at interop boundaries** (separate module) to **catch and
  translate** third-party exceptions into explicit status for the Core.
  Exceptions must never cross into Core code.
- The **only** sanctioned emission of `std::bad_alloc` inside Core is the
  global `operator new/new[]` overrides in `Core/Memory/GlobalNewDelete.cpp`; all
  other code must remain exception-free.

### Rationale
- Determinism and performance (no unwinding tables, no surprise slow paths).
- Portability across toolchains/platforms.
- Error contracts are visible at the call site.

### Patterns

```cpp
// Core API (status-based)
struct [[nodiscard]] Status {
  bool ok;
  const char* msg; // optional, static or caller-managed
};

[[nodiscard]] inline Status LoadFoo(BufferView src, Foo& out) noexcept;

// Interop boundary (exceptions ON only here)
[[nodiscard]] inline Status LoadVia3rdPartyNoThrow(const char* path) noexcept {
  try {
    ThirdParty::Load(path); // may throw
    return {true, nullptr};
  } catch (const std::exception& e) {
    DNG_LOG_ERROR("Interop", "3rd-party exception: {}", e.what());
    return {false, "3rd-party exception"};
  } catch (...) {
    DNG_LOG_ERROR("Interop", "3rd-party exception: <unknown>");
    return {false, "3rd-party exception"};
  }
}
```

**Build flags (Core targets):**

* MSVC: `/EHs-` (no C++ EH), keep SEH as needed; do **not** use `/EHsc` in Core.
* Clang/GCC: `-fno-exceptions`.

---

## 2) RTTI

### Policy

* **Forbidden in Core:** no `dynamic_cast`, no `typeid`.

### Alternatives

* **Static polymorphism:** concepts/CRTP/templates.
* **Tiny dynamic facades:** explicit V-tables (structs of function pointers).
* Optional lightweight `TypeId` (integral/UUID) when you need tagged unions.

**Build flags (Core targets):**

* MSVC: `/GR-`
* Clang/GCC: `-fno-rtti`

---

## 3) STL Usage

### Allowed with constraints

* Public headers **prefer views and POD** (`std::span`, pointers+sizes, simple
  structs). Avoid exposing owning standard containers in public ABI.
* Internal implementation can use a **curated subset**:

* Fundamentals: `<array> <span> <bit> <type_traits> <limits> <utility> <tuple> <optional> <variant> <string_view>`
* Containers if needed **and** wired to the engine allocator via an
  `AllocatorAdapter`: `<vector> <deque> <string> <unordered_map> <unordered_set>`
* Avoid heavy/locale/IO subsystems unless justified:
  **no** `<regex>`, `<filesystem>`, `<iostream>`, `<locale>`, `<future>`,
  `<thread>` in Core by default.

### Rules

* **No hidden allocations** on hot paths. Pre-reserve or use caller-provided
  buffers. Document complexity and allocation behavior in **Contract**.
* Do not introduce ABI that implicitly requires exceptions/RTTI (e.g.
  `std::function` with owning semantics) in Core public APIs.
* Prefer engine aliases/adapters when available.

---

## 4) Namespaces & Globals

* Everything lives under `namespace dng { ... }`.
* **No anonymous namespaces in public headers** (OK in `.inl` or private TU).
* No mutable global state in headers. Prefer `constexpr` data or explicit
  initialization functions and context objects.

---

## 5) Assertions, Logging, Diagnostics

* Programmer errors → `DNG_ASSERT(cond)`.
* Recoverable runtime conditions → `DNG_CHECK(cond)` and return a `Status`.
* Heavy logging under `Logger::IsEnabled("Category")` guards.

---

## 6) Build Profiles (suggested)

* **core_debug:** `-O0`, asserts ON, logs ON, `-fno-exceptions -fno-rtti`
  (`/EHs- /GR-` on MSVC).
* **core_release:** `-O3`, asserts OFF or light, logs minimal, same no-exceptions/no-rtti.
* **interop_exceptions (optional):** an **isolated** target where exceptions are
  ON to interface with throwing third-party libs; all exceptions are translated
  to `Status` before returning to Core.

---

## 7) Quick FAQ

* **Multiple inheritance?** Allowed (we are not bound by Unreal’s `UObject`
  constraint). Prefer composition; document virtual destructor/ABI choices.
* **Why not STL everywhere?** We do use STL—carefully. Public ABI should remain
  lean, allocator-aware, and free of implicit EH/RTTI requirements.
* **Why so strict on EH/RTTI?** Determinism, portability, binary size, and
  predictable cost—central to D-Engine’s teaching/auditing goals.

````
