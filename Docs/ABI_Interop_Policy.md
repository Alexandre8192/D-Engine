# D-Engine ABI and Interop Policy (v1)

Purpose
- Define a stable, language-agnostic plugin boundary for D-Engine.
- Keep the engine core in C++ (performance, control, pedagogy), while enabling modules/backends to be authored in other languages (Rust, Zig, C#, Python, etc.).
- Make the "interop story" boring, explicit, deterministic, and reviewable.

Contract (Non-negotiable)
- The only official cross-language boundary is a C ABI.
- No exceptions / RTTI in the Core, and absolutely no unwinding across the ABI boundary (no C++ exceptions, no Rust panics).
- No hidden allocations at the ABI boundary. Allocation ownership must be explicit.
- ABI v1 is frozen: once shipped, it is never modified in place. Compatible evolution uses v2/v3 names.

Notes
- This policy is written for both humans and AI contributors (Copilot).
- All examples and code snippets in this document are ASCII-only.

---

## 1. Terminology

Core
- The D-Engine runtime implemented in C++ (header-first, deterministic rules, no exceptions, no RTTI).

Contract (C++ contract)
- A backend-agnostic interface written in C++ for in-repo backends.
- C++ contracts are source-level stable, but not guaranteed ABI-stable across toolchains.

ABI Contract (C ABI)
- The stable binary boundary used for cross-language modules.
- This is the only boundary that may be promised to third parties as "stable".

Host
- The engine side that loads modules and provides host services (logging, allocation, etc.).

Module
- A dynamically loaded library (DLL/.so/.dylib) or statically linked component that implements one or more subsystem backends via the C ABI.

Subsystem ABI
- A function-table interface (vtable-like) for a specific domain (Window, Audio, Renderer, IO, etc.).

---

## 2. Design goals

G1 - Stable boundary, minimal surface
- The ABI should be as small as possible, and composed of trivial types.
- "Stable ABI" is a promise: we optimize for long-term compatibility.

G2 - Language agnostic by construction
- Any language that can call C can implement a module.
- The ABI avoids C++-specific features (name mangling, exceptions, templates, references).

G3 - Determinism-friendly
- Prefer explicit inputs/outputs, explicit clocks/ticks, explicit memory ownership.
- No hidden global state requirements at the ABI boundary.

G4 - Fast enough, but clarity first
- ABI calls are tables of function pointers, which is predictable and widely used.
- Hot-path design should remain in the C++ Core where possible.

---

## 3. Non-goals (explicitly NOT doing)

NG1 - "Multi-language engine core"
- The Core remains C++.
- We do not rewrite most of the engine in Rust.

NG2 - Cross-compiler C++ plugin ABI
- We do not promise that C++ modules compiled with arbitrary toolchains can link safely to the engine using C++ symbols.
- If third parties want to write C++ modules, they still go through the C ABI.

NG3 - Allowing unwind across ABI
- No exceptions/panics across the boundary, even if some platforms can technically do it.

---

## 4. The ABI pattern (the one true shape)

### 4.1 Function table + context (vtable-in-C)

Every subsystem ABI is a struct of function pointers plus a `void* ctx`:
- The Host stores (ctx + function table).
- Calls are indirect, like a vtable.
- This is the same fundamental pattern as COM and Vulkan dispatch tables.

### 4.2 Status codes, not exceptions

All ABI functions return a `dng_status_t` and use out-params for results:
- `DNG_STATUS_OK`, `DNG_STATUS_INVALID_ARG`, `DNG_STATUS_UNSUPPORTED`, etc.
- Never throw. Never unwind.

### 4.3 POD-only data

Allowed:
- Fixed-width integers, float/double.
- Structs containing only such fields (POD).
- Opaque handles (integers) that never expose internal pointers.

Disallowed:
- `std::string`, `std::vector`, references, templates, exceptions, virtual classes.
- Passing ownership via raw pointers without an explicit free path.

---

## 5. ABI type rules (copy-paste as review rules)

R1 - Use fixed-width types
- Use `uint32_t`, `uint64_t`, etc. in ABI headers.

R2 - No `bool` in ABI
- Use `uint8_t` for booleans (`0` or `1`).

R3 - Enums must have explicit underlying type
- Represent enums as `uint32_t` in the ABI if needed.

R4 - Strings are (ptr + len)
- No null-terminated requirement unless explicitly documented.
- Prefer `dng_str_view { const char* data; uint32_t size; }`.

R5 - Arrays are (ptr + count)
- Prefer `{ const T* data; uint32_t count; }`.

R6 - Handles are integers, not pointers
- Example: `typedef uint64_t dng_sound_handle;`
- `0` is invalid, unless documented otherwise.

R7 - Struct layout must be stable
- No packed structs by default.
- Include `struct_size` as the first field for extensibility if needed.

R8 - Alignment rules must be explicit
- If a struct requires special alignment, document it and validate it in tests.

---

## 6. Memory ownership and allocation rules

M1 - "Who allocates frees"
- If the Host allocates memory for a result, the Host must provide a matching free function.
- If a Module allocates memory for a result, it must also provide a free function or use Host allocators.

M2 - Prefer "caller-allocated output"
- Many APIs should accept a caller buffer:
  - first call returns required size
  - second call fills the buffer

M3 - Provide Host allocator services (recommended)
- Host exposes `alloc(size, align)` and `free(ptr, size, align)`.
- Modules that need memory use Host allocators, keeping ownership uniform.

M4 - No hidden allocation in hot paths
- ABI calls that may allocate must be documented as such.
- Hot-path calls should either:
  - use caller-provided scratch buffers, or
  - require pre-reserved arenas.

---

## 7. Error handling rules

E1 - Status codes only
- All errors cross the ABI as status codes.

E2 - Debug info is optional, not required
- If needed, provide `GetLastError` style functions in the subsystem API, but keep it optional and deterministic-friendly.

E3 - No unwind across ABI
- Any exception/panic must be caught inside its own language boundary and converted into status codes.

---

## 8. Versioning and compatibility

V1 - v1 is frozen
- After release, v1 symbols and structs must never change in place.

V2 - Breaks create new names
- Add `*_v2` structs, new entrypoints, and new negotiation.

V3 - Extend only at the end (optional pattern)
- If we choose "extend only", we must:
  - append fields only at the end
  - keep `struct_size` and validate it
  - never reorder or remove fields

---

## 9. Module entrypoint (minimum viable interface)

Rule: A module exposes exactly one C symbol per version:
- `dngModuleGetApi_v1(...)`

It returns:
- module metadata (name, version)
- one or more subsystem API tables (Audio, Window, etc.)

Pseudo-shape (illustrative):
- Host passes a `dng_host_api_v1*` with alloc/log services.
- Module fills a `dng_module_api_v1` output struct.

---

## 10. Repository layout (recommended)

Source/Core/Abi/
- Pure C headers that define:
  - base ABI types and macros
  - host API
  - subsystem APIs
  - module API entrypoints

Source/Core/Interop/
- C++ wrappers around the C ABI (header-only convenience):
  - type-safe wrappers
  - RAII for host-side management (but not part of ABI)

Source/Modules/
- Example modules:
  - NullAudioModule (C or C++)
  - (Optional) RustHelloModule (Rust) as a reference

docs/
- This policy file
- ABI review checklist
- "How to write a module" guide

---

## 11. Copilot rules (AI contributor constraints)

When generating or editing ABI code, Copilot MUST:
- Never modify a released v1 ABI struct or function signature.
- Never introduce non-POD types into ABI headers.
- Never add exceptions or unwinding across ABI.
- Use ASCII-only in code and comments.
- Always document:
  - Purpose / Contract / Notes
  - Ownership rules
  - Thread-safety rules
  - Determinism notes
- Add compile-time checks and smoke tests for struct sizes/offsets where relevant.

---

## 12. Quick examples (patterns only)

String view pattern:
- `ptr + len`, no implicit ownership transfer.

Two-call buffer fill pattern:
- call 1: get required size
- call 2: fill caller buffer

Function-table pattern:
- `api->DoThing(ctx, ...)`

---

## 13. "Red flags" (reject in review)

- Passing `std::string` / `std::vector` across ABI
- Returning ownership without a matching free function
- Any "TODO: catch exceptions later" crossing the boundary
- Changing v1 in place
- Using `bool` in ABI structs
- Exposing internal pointers as handles

---

End of policy.
