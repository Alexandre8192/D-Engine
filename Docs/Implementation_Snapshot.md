# Implementation Snapshot (as of 2026-02-10)

Purpose
- Capture the current implementation shape from code and smoke tests.
- Provide a stable "where things really are" reference for feature work.

Scope note
- This snapshot is code-backed (`Source/`, `tests/`, `.vcxproj` files).
- Milestone docs like `*_M0_Status.md` and `Progress_Summary_v0.1.md` are historical snapshots and may not reflect current behavior.

## Current architecture
- Core pattern per subsystem:
  - Contract: `Source/Core/Contracts/<Subsystem>.hpp`
  - Null backend: `Source/Core/<Subsystem>/Null<Subsystem>.hpp`
  - Orchestrator: `Source/Core/<Subsystem>/<Subsystem>System.hpp`
- Runtime orchestration:
  - `Source/Core/Runtime/CoreRuntime.hpp`
  - Init order: `Memory -> Time -> Jobs -> Input -> Window -> FileSystem -> Audio -> Renderer`
  - `InitCoreRuntime` rolls back on failure; `ShutdownCoreRuntime` is idempotent.

## Subsystem reality (high level)
- Memory
  - `Source/Core/Memory/MemorySystem.hpp` is a central orchestrator with multiple allocator backends and policy hooks.
- Audio
  - `Source/Core/Audio/AudioSystem.hpp` supports Null, Platform (WinMM), and External backends.
  - `Source/Core/Audio/WinMmAudio.cpp` provides real device output + software mixing + stream support.
- Renderer
  - `Source/Core/Renderer/NullRenderer.hpp` + `Source/Core/Renderer/RendererSystem.hpp` are active.
  - `Source/Modules/Renderer/Rendering/BasicForwardRenderer/BasicForwardRenderer.hpp` is a telemetry/stub backend, not a full GPU renderer.
- Time, Jobs, Input, Window, FileSystem
  - Contract + Null + System path is complete and heavily smoke-tested.
  - External interface injection paths are available in their system initializers.
- ABI and modules
  - ABI surface in `Source/Core/Abi/`.
  - Loader/adapters in `Source/Core/Interop/`.
  - Module smoke target validates DLL loading and API table verification (`tests/Abi/ModuleSmoke.cpp`).

## Test and target reality
- Aggregate smoke executable:
  - `tests/AllSmokes/AllSmokes_main.cpp` -> `AllSmokes.exe`
  - Runs subsystem smokes, determinism replay smoke, and stable memory runtime smokes.
- Extended memory stress executable:
  - `tests/MemoryStressSmokes/MemoryStressSmokes_main.cpp` -> `MemoryStressSmokes.exe`
  - Runs long-running/noisy memory stress scenarios separately from `AllSmokes`.
- ABI smoke executable:
  - `tests/Abi/ModuleSmoke.cpp` -> `ModuleSmoke.exe`
- Benchmark executable:
  - `tests/BenchRunner/D-Engine-BenchRunner.vcxproj` -> `D-Engine-BenchRunner.exe`
- Compile/build-only checks:
  - Header self-containment under `tests/SelfContain/`
  - Policy checks under `tests/Policy/`
  - Included through the solution/project build graph.

## Feature implementation workflow (practical)
1. Extend or add the contract in `Source/Core/Contracts/`.
2. Implement/update Null backend and subsystem system layer.
3. Wire runtime integration in `Source/Core/Runtime/CoreRuntime.hpp` when lifecycle-wide behavior is needed.
4. Add or update smoke coverage in `tests/Smoke/...` and aggregator wiring if runnable behavior changed.
5. Update docs:
   - this file (`Implementation_Snapshot.md`) for global behavior changes,
   - subsystem status doc(s) for milestone/history context.
