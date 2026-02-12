# AGENTS.md

Guidance for agentic coding assistants working in D-Engine.
Consolidates repo rules + Copilot instructions for Core code.

## Scope
- Applies repo-wide unless a nearer `AGENTS.md` overrides.
- Cursor rules: none found in `.cursor/rules/` or `.cursorrules`.
- Copilot rules source: `.github/copilot-instructions.md`.

## Quick Links
- `README.md` — build overview.
- `D-Engine_Handbook.md` — architecture map.
- `Docs/LanguagePolicy.md` — Core language rules.
- `Docs/HeaderFirstStrategy.md` — header-first patterns.
- `Docs/DeterminismPolicy.md` — determinism rules.
- `Docs/ThreadingRules.md` — threading rules.
- `tests/README.md` — test layout.

## Repo Map (mental model)
- `Source/Core/Contracts/` — subsystem contracts (public API).
- `Source/Core/<Subsystem>/` — systems + null backends.
- `Source/Core/Abi/` + `Source/Core/Interop/` — ABI surface and adapters.
- `Source/Modules/` — optional or loadable backends.
- `tests/` — header-only compile checks and smoke helpers.
- `Docs/` — policies, status docs, and design notes.

## Build Commands (MSVC/MSBuild)
- Debug build: `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64 /m`.
- Release build: `msbuild D-Engine.sln /p:Configuration=Release /p:Platform=x64 /m`.
- Build only the engine lib: `msbuild D-Engine.vcxproj /p:Configuration=Debug /p:Platform=x64`.
- Build AllSmokes only: `msbuild D-Engine.sln /t:AllSmokes /p:Configuration=Debug /p:Platform=x64`.
- Build MemoryStressSmokes only: `msbuild D-Engine.sln /t:MemoryStressSmokes /p:Configuration=Debug /p:Platform=x64`.
- Build ModuleSmoke only: `msbuild D-Engine.sln /t:ModuleSmoke /p:Configuration=Debug /p:Platform=x64`.
- Build BenchRunner only: `msbuild D-Engine.sln /t:D-Engine-BenchRunner /p:Configuration=Release /p:Platform=x64`.
- Mem tracking compile-only: `msbuild D-Engine.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:PreprocessorDefinitions="DNG_MEM_TRACKING=1;DNG_MEM_CAPTURE_CALLSITE=1;%(PreprocessorDefinitions)" /p:BuildProjectReferences=false`.

## Lint Commands (Policy)
- Base: `python tools/policy_lint.py`.
- Strict: `python tools/policy_lint.py --strict`.
- Strict + modules: `python tools/policy_lint.py --strict --modules`.
- Self-test: `python tools/policy_lint_selftest.py`.

## Test/Smoke Commands
- All smokes (after build): `x64\Debug\AllSmokes.exe` or `x64\Release\AllSmokes.exe`.
- Memory stress smokes: `x64\Debug\MemoryStressSmokes.exe` or `x64\Release\MemoryStressSmokes.exe`.
- ABI smoke: `x64\Debug\ModuleSmoke.exe` or `x64\Release\ModuleSmoke.exe`.
- ModuleSmoke requires `NullWindowModule.dll` built by the solution (or build the Rust module).
- Header-only and build-only tests compile with the solution build.

## Single-Test Guidance
- Smallest runnable units are executables (AllSmokes, MemoryStressSmokes, ModuleSmoke, BenchRunner).
- To focus on a single smoke, temporarily edit `tests/AllSmokes/AllSmokes_main.cpp` to call only the desired `Run*Smoke()` (do not commit).
- Prefer building the specific project target (`/t:AllSmokes`, `/t:MemoryStressSmokes`, `/t:ModuleSmoke`) to reduce build time.

## Bench Commands
- Build Release: `msbuild D-Engine.sln -m -p:Configuration=Release -p:Platform=x64`.
- Run bench: `x64\Release\D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 7`.
- Optional output: set `DNG_BENCH_OUT=artifacts/bench` before running.
- Memory sweep + compare: `python tools/memory_bench_sweep.py --strict-stability --stabilize --compare-baseline`.
- Capture baseline candidates (safe default): `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both`.
- Promote baselines after review: `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both -Promote`.

## Gate Script (all-in-one)
- Fast gates: `pwsh tools/run_all_gates.ps1 -Fast` (skips `MemoryStressSmokes` and bench).
- Full gates: `pwsh tools/run_all_gates.ps1` (includes `MemoryStressSmokes`).
- Include bench: `pwsh tools/run_all_gates.ps1 -RequireBench`.
- Build Rust module before ModuleSmoke: `pwsh tools/run_all_gates.ps1 -RustModule`.

## Code Style (Core Rules, per Copilot Instructions)
- Language: C++23/26, header-first; `.cpp` only when technically required.
- Headers are self-contained; no hidden includes; use engine-absolute paths like `Core/...`.
- No exceptions and no RTTI in Core (`throw`, `try`, `catch`, `dynamic_cast`, `typeid` forbidden).
- No raw `new`/`delete` in Core; use D-Engine allocators (`AllocatorRef::New`, etc.).
- Raw pointers in Core are non-owning views; document ownership explicitly.
- Do not use `std::shared_ptr`/`std::weak_ptr` in Core; `std::unique_ptr` only in tools/tests or cold paths.
- Namespace: `dng::` for code; macros use `DNG_` prefix.
- Default to `[[nodiscard]]`, `constexpr`, `noexcept` when sensible; enforce invariants with `static_assert`.
- Comments/identifiers must be ASCII-only.
- Avoid heavy STL subsystems (`<regex>`, `<filesystem>`, `<iostream>`, `<locale>`) without a Design Note.
- No hidden allocations in hot paths; document allocation behavior in Contracts.

## Formatting and Layout
- Use `#pragma once` in headers.
- Follow existing Allman-style braces (opening brace on next line).
- Indentation: 4 spaces; align with surrounding code.
- Keep include order: standard headers, third-party (rare), engine headers.
- Prefer explicit includes (IWYU); avoid reliance on transitive includes.
- No anonymous namespaces in public headers.

## Naming Conventions
- Types: `PascalCase` (e.g., `AllocatorRef`).
- Functions/methods: `PascalCase` in Core (match existing headers).
- Local variables: `lowerCamelCase`.
- Members: `m_PascalCase` or `m_CamelCase` (match existing `m_Alloc`).
- Namespaces: `dng` / `dng::core` as used in existing files.
- Macros: `DNG_SCREAMING_SNAKE_CASE`.
- Files: `PascalCase` for Core headers; tests use suffixes like `_smoke.cpp` and `_header_only.cpp`.

## Types and API Design
- Prefer `enum class` over plain enums.
- Avoid implicit conversions; prefer explicit constructors/helpers.
- Consider strong typedefs for IDs, sizes, and units.
- Prefer POD views (spans, pointer+count) in public APIs; avoid owning STL types.
- Prefer free functions and small POD structs to deep inheritance.

## Contracts, Docs, and File Headers
- Public symbols must include Purpose/Contract/Notes blocks.
- New public headers need a file header using the template below.
- Keep docs and behavior in sync.

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

## Error Handling and Diagnostics
- Use explicit status types; avoid exceptions.
- `DNG_ASSERT` for invariants/programmer errors.
- `DNG_CHECK` for recoverable errors and return status.
- Gate heavy logging with `Logger::IsEnabled()` to avoid allocation costs.
- Keep OOM behavior consistent with Memory policy (`DNG_MEM_CHECK_OOM`).

## Memory and Ownership
- All allocations go through D-Engine allocators; `free` must match size/alignment.
- Use `NormalizeAlignment`, `AlignUp`, `IsPowerOfTwo` from `Alignment.hpp`.
- Prefer values, handles (index+generation), or engine RAII wrappers for ownership.
- Avoid hidden allocations in hot loops; pre-reserve or use caller-provided buffers.

## Performance and Determinism
- Prefer data-oriented layouts and predictable branching.
- Avoid virtual dispatch/type-erasure in hot paths unless justified with a cost model.
- Determinism matters: follow `Docs/DeterminismPolicy.md` and `Docs/ThreadingRules.md`.
- Stable ordering is required when results depend on iteration.

## Templates and Build Hygiene
- Keep heavy templates in `detail/` headers; avoid including them in public headers.
- Use `extern template` for finite template instantiations to reduce rebuilds.

## Tests and Bench Expectations
- Add a compile-only smoke test in `tests/` for each new public header.
- Perf-sensitive changes should add a `DNG_BENCH` snippet or update bench harness.
- Ensure AllSmokes and ModuleSmoke executables run cleanly after changes.

## Anti-Patterns (Reject/Flag)
- Exceptions/RTTI in Core.
- Raw `new`/`delete` or `std::shared_ptr` in Core.
- Hidden allocations in hot paths or constructors doing I/O.
- Heavy STL subsystems without a Design Note.
- Opaque macros that hide control flow.

## When Unsure
- Prefer clarity and explicit contracts over cleverness.
- Follow existing patterns in `Source/Core` and update docs when behavior changes.
- Ask for confirmation before broad refactors or new dependencies.
