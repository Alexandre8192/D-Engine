# D-Engine — Language Policy (Repository)

> Goal: Deterministic, auditable, portable C++23/26 with zero hidden costs.
> This document defines what D-Engine code allows and why.
> The policy applies to the entire repository:
> Source/, Backends/, Tests/, Examples/, Tools/.

---

## TL;DR

- No C++ exceptions anywhere. No throw, no try/catch.
- No RTTI anywhere. No dynamic_cast, no typeid.
- Explicit error handling only: status-based APIs.
- Public APIs expose POD and views, never owning STL containers.
- No hidden allocations in hot paths.
- Determinism by design: time, randomness, IO go through engine services only.
- Enforcement is automatic: compiler flags + CI scans.

---

## 1) Exceptions

### Policy

C++ exceptions are forbidden across the entire D-Engine repository.

This includes:
- Source/
- Backends/
- Tests/
- Examples/
- Tools/

Forbidden constructs:
- throw
- try / catch
- deprecated dynamic exception specifications (noexcept is allowed)
- APIs that require exceptions to function correctly

No code in this repository is allowed to rely on exception-based control flow.
APIs whose normal operation relies on stack unwinding or exception propagation are forbidden.

### Rationale

- Determinism: no hidden control flow or stack unwinding.
- Portability: not all platforms/toolchains support exceptions uniformly.
- Performance predictability: no hidden tables, no surprise slow paths.
- Auditability: all failure paths are explicit at the call site.
- Teaching value: users learn to reason about failure explicitly.

### Required Pattern

All fallible operations must return an explicit status.

```cpp
struct [[nodiscard]] Status {
  bool ok;
  const char* msg; // optional, static or caller-managed
};

[[nodiscard]] Status LoadFoo(BufferView src, Foo& out) noexcept;
```

Fatal conditions are handled explicitly via:

* DNG_ASSERT (programmer error)
* DNG_CHECK + Status return (recoverable error)
* Engine-defined abort paths (OOM, contract violation)

---

## 2) RTTI

### Policy

RTTI is forbidden across the entire repository.

Forbidden constructs:

* dynamic_cast
* typeid
* std::type_info

### Rationale

* Predictable binary layout and ABI.
* No hidden runtime metadata.
* Encourages explicit, intentional polymorphism.

### Allowed Alternatives

* Static polymorphism (templates, concepts, CRTP).
* Explicit v-tables (structs of function pointers).
* Tagged unions (enum + union / variant-like POD).
* Engine-defined TypeId (integral or hashed), if needed.

---

## 3) STL Usage

### General Rule

The STL is allowed, but never implicitly.

### Public APIs

Public headers must expose:

* POD types
* views (pointer + size, Span, StringView)
* engine-defined lightweight abstractions

Public APIs must not expose:

* owning STL containers
* types that require exceptions or RTTI
* allocator-opaque abstractions

### Internal Usage

Internal code may use a curated subset of the STL:

Allowed headers:

* <array>, <span>, <bit>, <type_traits>, <limits>
* <utility>, <tuple>, <optional>, <variant>
* <string_view>

Containers are allowed only if:

* they are wired to the engine allocator
* their allocation behavior is documented
* they are not used in hot paths without preallocation

Avoid by default in Core logic (justify if used):

* <regex>, <filesystem>, <iostream>, <locale>
* <future>, <thread>

---

## 4) Memory & Allocation

* No hidden allocations in hot paths.
* Allocation must be explicit, visible, and documented.
* APIs must state allocation behavior in their Contract.
  This includes:
  - whether allocation occurs
  - when it occurs
  - under which conditions it may fail
* Callers must control lifetime and ownership.

OOM behavior is engine-defined and explicit.
No allocation failure is allowed to escape implicitly.

---

## 5) Determinism

* No direct use of std::rand, std::chrono, OS clocks, or global state.
* Time, randomness, IO, and threading go through engine services.
* Deterministic backends are the default.

Note: Determinism is evaluated at the engine contract level, not at the OS level.

---

## 6) Namespaces & Globals

* All symbols live under namespace dng.
* No anonymous namespaces in public headers.
* No mutable global state in headers.
* Use constexpr data or explicit initialization APIs.

---

## 7) Assertions & Diagnostics

* Programmer errors: DNG_ASSERT
* Recoverable errors: DNG_CHECK + Status
* Logging must be explicit and guarded.

---

## 8) Enforcement

This policy is enforced by:

* Compiler flags applied per target:

  * -fno-exceptions / -fno-rtti (Clang/GCC)
  * /EHs- /GR- (MSVC)
* Force-included policy header (every translation unit): Source/Core/Policy/LanguagePolicy.hpp
* CMake helper `dng_enforce_language_policy(target)` applies both the force-include and the disable-EH/RTTI flags to every engine-owned target.
* CI forbidden-pattern scanning

Violations are build errors.

---

## Final Note

D-Engine is intentionally strict.
If you need exceptions, RTTI, or opaque abstractions,
they belong outside of this repository.
