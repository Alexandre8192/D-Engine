# D-Engine Handbook (single source of truth)

Purpose
- Explain what D-Engine is trying to be, and what it is NOT trying to be.
- Explain the Contract model (backend interchangeability) for in-process C++ backends.
- Explain the ABI model (loadable modules) and how it enables future bindings from other languages.
- Point to the concrete files in this repository that implement the ideas.

Audience
- Contributors and reviewers (including AI assistants) who want a stable mental model.
- Users who want to understand how to plug a backend into D-Engine without reading every header.

Status
- The repository keeps a contract-first core, but implementation maturity is uneven by subsystem.
- For current behavior, prefer `Docs/Implementation_Snapshot.md`, `Source/`, and smoke targets over milestone snapshot docs.

-------------------------------------------------------------------------------

## 1. Philosophy

D-Engine is built around a few non-negotiables:

1) Header-first, contracts first
- Public APIs live in headers.
- Every subsystem starts with a Contract that describes: data, handles, invariants, and the call surface.
- Implementations (backends) are interchangeable and are not allowed to leak into the contract layer.

2) "Freedom in C++", but with discipline
- No magic.
- Explicit ownership, explicit lifetimes, explicit costs.
- If something is optional, it is actually optional: you can remove a module and the rest still builds.

3) Determinism is a first-class goal
- Null backends exist to be deterministic and testable.
- If a subsystem cannot guarantee determinism, that must be stated in its caps / docs.

4) Performance by design, not by micro-tricks
- No hidden allocations on hot paths.
- No exceptions and no RTTI in Core.
- Data layout is explicit; handles are small; views are non-owning.

5) Documentation is part of the product
- Code is expected to carry "Purpose / Contract / Notes" comments where it matters.
- Tests include "header-only" compile tests to keep the header-first promise honest.

If you only remember one sentence:
- D-Engine is a set of well-explained building blocks that can be wired together, not a monolith.

-------------------------------------------------------------------------------

## 2. Terms and Layers (glossary)

Contract
- A backend-agnostic description of a subsystem (types + functions + invariants).
- Lives under: Source/Core/Contracts/
- Example: Source/Core/Contracts/Window.hpp

Backend
- A concrete implementation of a Contract.
- Backends can be:
  - In-process C++ (header or compiled .cpp) implementing the contract.
  - A loadable module that exposes a C ABI table (see ABI section).

Null Backend
- A deterministic reference backend used for tests and as the default wiring.
- Lives near each subsystem:
  - Source/Core/<Subsystem>/Null*.hpp
  - Example: Source/Core/Window/NullWindow.hpp

System (orchestrator)
- Owns state, validates configuration, and routes calls to the chosen backend.
- It is the main "runtime object" for a subsystem in Core.
- Example: Source/Core/Window/WindowSystem.hpp

ABI (Application Binary Interface)
- A stable C interface shape for loadable modules and cross-language interop.
- Lives under: Source/Core/Abi/
- C ABI is the canonical cross-language contract surface.

Module (loadable)
- A shared library (DLL/.so/.dylib) that exports a module entrypoint function.
- The entrypoint returns one or more subsystem function tables.
- Example: Source/Modules/Window/NullWindowModule/NullWindowModule.cpp

Interop Helpers
- C++ helpers that load a module and adapt ABI tables into a friendly C++ face.
- Lives under: Source/Core/Interop/
- Example: Source/Core/Interop/ModuleLoader.hpp

-------------------------------------------------------------------------------

## 3. Repository Map (what lives where)

Root
- README.md
  - Quickstart, non-negotiables, and a high level overview.
- Docs/
  - Policies, implementation snapshots, milestone snapshots, and deeper design notes.
  - Docs/INDEX.md (map of all docs)
- Source/
  - Core/ : the engine core building blocks and contracts.
  - Modules/ : optional modules (loadable or in-process backends).
- tests/
  - Header self-containment checks, smoke tests, and policy compile tests.

Core (Source/Core)
- Platform/ : fixed width types, platform macros, low-level detection.
- Diagnostics/ + Logger.hpp + Timer.hpp : checks/logging/timers (core utilities).
- Memory/ : allocator suite and runtime override rules.
- Math/ : foundational math types and functions.
- Containers/ : engine containers (when used).
- Contracts/ : subsystem contracts (Window, Renderer, Time, Jobs, Input, FileSystem, Audio).
- <Subsystem>/ : per-subsystem orchestrator + Null backend.
- Abi/ : stable C ABI headers (interop contract surface).
- Interop/ : dynamic loader and ABI adapters (C++ convenience layer).

Subsystem pattern (current code)
- Contract: Source/Core/Contracts/<Subsystem>.hpp
- Null backend: Source/Core/<Subsystem>/Null<Subsystem>.hpp
- System: Source/Core/<Subsystem>/<Subsystem>System.hpp
- Tests:
  - tests/Smoke/Subsystems/*_smoke.cpp
  - tests/Smoke/Memory/*_smoke.cpp
  - tests/Smoke/Determinism/*_smoke.cpp
  - tests/SelfContain/*_header_only.cpp
  - tests/AllSmokes/AllSmokes_main.cpp (aggregate executable for stable smokes)
  - tests/MemoryStressSmokes/MemoryStressSmokes_main.cpp (aggregate executable for aggressive memory stress)
- Historical milestone status docs: Docs/<Subsystem>_M0_Status.md

Example (Window)
- Contract: Source/Core/Contracts/Window.hpp
- Null backend: Source/Core/Window/NullWindow.hpp
- System: Source/Core/Window/WindowSystem.hpp
- ABI types and tables: Source/Core/Abi/DngWindowApi.h
- Loadable sample module: Source/Modules/Window/NullWindowModule/NullWindowModule.cpp
- Interop wrappers: Source/Core/Interop/WindowAbi.hpp

-------------------------------------------------------------------------------

## 4. Contracts and Backend Interchangeability (in-process C++)

### 4.1 The contract is the boundary

A Contract header:
- defines POD-ish types (descriptors, handles, views),
- defines invariants and expectations (threading, determinism, error semantics),
- defines the call surface (functions) without forcing a specific backend.

Constraints at the contract boundary:
- no exceptions and no RTTI (Core rule),
- avoid owning STL containers in the public surface (prefer views, spans, ptr+count),
- do not hide allocations at the boundary,
- handle lifetime rules are explicit.

### 4.2 Two faces: static and dynamic (when needed)

D-Engine prefers a dual-face approach:

Static face (compile-time)
- Use templates / concepts where possible.
- Best performance and best inlining.
- The user chooses the backend at compile time.

Dynamic face (runtime)
- A small vtable-style interface for runtime backend selection.
- Useful for testing, tools, and late binding.

Many contracts in this repo include helpers for dynamic selection
without forcing virtual classes or RTTI.

### 4.3 Systems wire contracts to backends

A System:
- holds the chosen backend interface,
- holds subsystem state,
- applies runtime policy (validation, defaulting),
- exposes an easy API for the rest of Core.

This keeps contracts thin and keeps backends replaceable.

### 4.4 Null backends define "minimum viable semantics"

Null backends are not toys:
- they provide deterministic reference behavior,
- they are used by smoke tests,
- they are the default wiring when you "just want it to run".

If a real backend differs, it must document the differences (caps and notes).

-------------------------------------------------------------------------------

## 5. ABI and Loadable Modules (cross-language foundation)

### 5.1 Why the ABI exists

The ABI is a second, stricter boundary that enables:
- loadable modules (plugins) without C++ ABI issues,
- potential backends written in other languages,
- stable interop for tooling and embedding.

The ABI is not meant to replace in-process C++ contracts.
It is an optional interop path.

### 5.2 The one true ABI shape

The ABI follows a consistent pattern:
- function table + context (a vtable-in-C),
- explicit status codes (no exceptions),
- POD-only data (fixed width integers, explicit bool size),
- explicit ownership (host alloc/free, caller-allocated outputs),
- no unwinding across the boundary.

Canonical headers
- Base types and macros: Source/Core/Abi/DngAbi.h
- Host services: Source/Core/Abi/DngHostApi.h
- Subsystem table (example Window): Source/Core/Abi/DngWindowApi.h
- Module entrypoint: Source/Core/Abi/DngModuleApi.h

### 5.3 Module entrypoint

A loadable module exports a single C function:

- dngModuleGetApi_v1(out_api)

It fills a dng_module_api_v1 struct with:
- module metadata (name + version),
- one or more subsystem function tables (currently Window is the pilot).

See:
- Source/Core/Abi/DngModuleApi.h
- Source/Modules/Window/NullWindowModule/NullWindowModule.cpp

### 5.4 Host API (services provided to modules)

The host passes a dng_host_api_v1 table to subsystem create/init calls
(depending on the subsystem contract), so modules can:
- log via host,
- allocate/free via host allocator.

See:
- Source/Core/Abi/DngHostApi.h

### 5.5 Interop helpers in C++

Loading and calling ABI modules from C++ is supported via:
- Source/Core/Interop/ModuleLoader.hpp (+ .cpp)
- Source/Core/Interop/WindowAbi.hpp

These helpers are cold-path by design:
- dynamic loading is not a hot path,
- they trade a bit of overhead for clarity and safety.

-------------------------------------------------------------------------------

## 6. Writing a backend in another language (future-proof rules)

This section describes how to approach "contracts in other languages".
The short answer:
- Use the C ABI as the canonical contract boundary, and generate or hand-write bindings.

### 6.1 Rule: the ABI headers are the source of truth

If you want a backend in Rust/Zig/C#/etc:
- you implement the ABI tables defined in Source/Core/Abi/
- you export dngModuleGetApi_v1
- you follow the ABI Policy and Checklist in Docs/

Policy and checklist
- Docs/ABI_Interop_Policy.md
- Docs/ABI_Review_Checklist.md

### 6.2 Data layout rules you must respect

- Use fixed width integers (u8/u16/u32/u64, i8/i16/i32/i64).
- Use explicit bool size (uint8_t or u8), never language-native "bool" unless it is guaranteed to be 1 byte and repr(C) compatible.
- Strings are views: pointer + length (not null-terminated by default).
- Arrays are views: pointer + count.
- Handles are integers (or opaque structs holding an integer).
- No packed structs unless the ABI header explicitly packs (prefer natural alignment).

### 6.3 Calling convention and exports

- Use the calling convention macro from DngAbi.h (DNG_ABI_CALL) when implementing ABI functions.
- Export symbols using the ABI export macro (DNG_ABI_API / DNG_ABI_EXPORT as defined).
- Never throw across the boundary. If your language can panic/throw, you must catch and translate to a status code.

### 6.4 Ownership and allocation rules

- If the ABI says "caller allocates", the caller must pass a buffer and capacity.
- If the module returns allocated memory, it must use host->alloc and host->free with matching size+align.
- Never mix allocators across boundaries.

### 6.5 Testing strategy for cross-language backends

Minimum requirements:
1) Compile check: the bindings compile and match the C headers.
2) Size checks: verify struct sizes and alignments (static asserts where possible).
3) Smoke test: load the module and call a basic sequence (create, query, destroy).

In this repo, the C/C++ header self-containment tests are the model:
- tests/Abi/AbiHeaders_c.c
- tests/Abi/AbiHeaders_cpp.cpp
- tests/Abi/ModuleSmoke.cpp

If you add a Rust backend, add a similar "smoke loader" test that loads it.

-------------------------------------------------------------------------------

## 7. Versioning and compatibility rules (how we evolve safely)

D-Engine uses two kinds of versioning:

Subsystem contract versioning (C++ headers)
- Controlled by repository version (v0.1 currently).
- Breaking changes are allowed at v0.x but should be documented.

ABI versioning (C headers)
- Must be explicit and conservative.
- ABI structs include:
  - struct_size
  - abi_version
- Function tables are versioned (v1, v2, ...).
- Additive evolution is preferred:
  - add new fields at the end,
  - keep existing fields stable,
  - consider reserved fields for growth.

See:
- Docs/ABI_Interop_Policy.md (Versioning and compatibility section)

-------------------------------------------------------------------------------

## 8. How to add a new subsystem (end-to-end recipe)

Goal: every new subsystem follows the same predictable pattern.

Step 1: Contract (Source/Core/Contracts/<Subsystem>.hpp)
- Define:
  - POD-ish descriptors and handles
  - capability struct (caps)
  - required functions
  - thread-safety and determinism notes
- Provide:
  - static face (template/concept) if feasible
  - optional dynamic face helper if runtime selection matters

Step 2: Null backend (Source/Core/<Subsystem>/Null<Subsystem>.hpp)
- Deterministic reference behavior.
- Minimal state, explicit behavior, no side effects.
- No hidden allocations in hot paths.

Step 3: System orchestrator (Source/Core/<Subsystem>/<Subsystem>System.hpp)
- Validates config.
- Holds backend interface.
- Provides a simple runtime API.

Step 4: Tests
- Header self-containment (compile-only):
  - tests/SelfContain/*_header_only.cpp
- Smoke tests:
  - tests/Smoke/Subsystems/*_smoke.cpp
  - tests/Smoke/Memory/*_smoke.cpp
  - tests/Smoke/Determinism/*_smoke.cpp
  - tests/AllSmokes/AllSmokes_main.cpp (for aggregate stable execution)
  - tests/MemoryStressSmokes/MemoryStressSmokes_main.cpp (for aggregate memory stress execution)
- If ABI is involved:
  - tests/Abi/AbiHeaders_c.c and tests/Abi/AbiHeaders_cpp.cpp
  - tests/Abi/ModuleSmoke.cpp

Step 5: Docs
- Update Docs/Implementation_Snapshot.md when runtime behavior or architecture wiring changes.
- Keep Docs/<Subsystem>_M0_Status.md only as a milestone snapshot when relevant.

-------------------------------------------------------------------------------

## 9. Policies and where they live

Core language and safety rules
- Docs/LanguagePolicy.md (this should be pure markdown policy text)

ABI rules
- Docs/ABI_Interop_Policy.md
- Docs/ABI_Review_Checklist.md

Header-first constraints
- Docs/HeaderFirstStrategy.md

Benchmark rules (CI stability)
- Docs/Benchmarks.md
- .github/PR_VALIDATION.md
- .github/workflows/bench-*.yml

Subsystem vision docs
- Docs/Renderer_Vision.md (renderer direction)
- Docs/Implementation_Snapshot.md (current code-backed status)
- Docs/*_M0_Status.md (historical milestone snapshots)

-------------------------------------------------------------------------------

## 10. "Coherence" checklist (what reviewers should enforce)

Documentation coherence
- There is exactly one "handbook" (this file) that explains the big picture.
- README.md stays as a quick entrypoint and points here for architecture/interop.
- Policies are consistent and do not contradict each other.
- Terms are consistent: Contract, Backend, System, Module, ABI.

Code coherence
- Contracts stay in Source/Core/Contracts/.
- Every subsystem has: Contract + Null backend + System + tests.
- No exceptions and no RTTI in Core.
- No hidden allocations across contract boundaries.
- Headers are self-contained (compile-only tests stay green).
- ABI changes follow the checklist and keep compatibility rules.

Tooling coherence
- Copilot instructions reference policies instead of duplicating them.
- Bench CI rules match Docs/Benchmarks.md and PR_VALIDATION.md.

-------------------------------------------------------------------------------

## 11. Appendix: Where to look next

If you are new:
- Start at README.md (quickstart).
- Then read Docs/Implementation_Snapshot.md for current implementation reality.
- Then read this handbook to understand layering and rationale.
- Then inspect a contract header in Source/Core/Contracts/.

If you are adding a backend:
- In-process: implement the Contract and wire it through the System.
- Loadable: implement the ABI table and export dngModuleGetApi_v1.

If you are reviewing:
- Use Docs/ABI_Review_Checklist.md for anything crossing the ABI boundary.
- Use Docs/LanguagePolicy.md for Core safety rules.
