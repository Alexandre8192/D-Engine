# Tests

This folder collects the compile-only and smoke helpers that keep the header-first promise honest.

## Layout
- SelfContain/: header-only translation units; no `main`; each includes a single public header to validate self-containment.
- Smoke/Subsystems/: subsystem smoke helpers (Window, Time, Jobs, Input, Audio, AudioPlayback, FileSystem, RendererSystem, BasicForwardRenderer, CoreRuntime); no `main`; consumed by the smoke aggregators.
- Smoke/Determinism/: determinism/replay smokes; no `main`.
- Smoke/Memory/: allocator and memory-policy smokes; no `main`.
- Abi/: ABI conformance/interop compilation helpers.
- Policy/: policy-violation checks (expected-to-fail builds) that document guardrails.
- AllSmokes/: smoke aggregator executable entrypoint (`AllSmokes_main.cpp`) that runs subsystem + determinism + selected memory runtime smokes.
- Abi/ModuleSmoke.vcxproj: ABI module loader executable (`ModuleSmoke.exe`) that validates loadable module wiring.
- BenchRunner/: benchmark executable target (`D-Engine-BenchRunner.exe`).
- Math/: targeted math coverage and convention checks.

## Running
- Build the solution (e.g., `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64`) to compile header-only and smoke helpers.
- Run `x64\\Debug\\AllSmokes.exe` (or Release) for aggregate smoke coverage.
- Run `x64\\Debug\\ModuleSmoke.exe` (or Release) for ABI module smoke coverage.
- Run `x64\\Release\\D-Engine-BenchRunner.exe` for benchmark coverage.
- For focused memory perf runs:
  - `x64\\Release\\D-Engine-BenchRunner.exe --memory-only --memory-matrix --cpu-info`
  - `python tools/bench_sweep.py --memory-only --memory-matrix --strict-stability`
  - `python tools/memory_bench_sweep.py --strict-stability`

## Memory Coverage
- Runtime memory smokes executed by `AllSmokes`: `ArenaAllocator`, `FrameAllocator`, `StackAllocator`, `SmallObjectAllocator`, `LoggerOnly`, `GuardAllocatorAlignment`, `AllocatorAdapter`, `FrameScope`, `MemorySystem`, `NewDelete`, `OOMPolicy`, `PageAllocator`, `PoolAllocator`, `SmallObjectTLSBins`, `TrackingAllocator`.
- Build-only memory checks compiled with `D-Engine.vcxproj`: `Test_MemoryRuntimeOverrides_BuildOnly.cpp`, `SelfContain/SmallObjectAllocator_header_only.cpp`, and the memory self-contain/header-order tests under `tests/SelfContain/`.
- Bench memory performance coverage is provided by `BenchRunner` (`ArenaAllocReset`, `FrameAllocReset`, `PoolAllocFreeFixed`, `SmallObjectAllocFreeSmall`, `TrackingOverheadSmallAlloc`).
