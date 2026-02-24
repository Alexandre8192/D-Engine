# Tests

This folder collects the compile-only and smoke helpers that keep the header-first promise honest.

## Layout
- SelfContain/: header-only translation units; no `main`; each includes a single public header to validate self-containment.
- Smoke/Subsystems/: subsystem smoke helpers (Window, Time, Jobs, Input, Audio, AudioPlayback, FileSystem, RendererSystem, BasicForwardRenderer, CoreRuntime); no `main`; consumed by the smoke aggregators.
- Smoke/Determinism/: determinism/replay smokes; no `main`.
- Smoke/Memory/: allocator and memory-policy smokes; no `main`.
- Abi/: ABI conformance/interop compilation helpers.
- Policy/: policy-violation checks (expected-to-fail builds) that document guardrails.
- AllSmokes/: smoke aggregator executable entrypoint (`AllSmokes_main.cpp`) for subsystem + determinism + stable memory runtime smokes.
- MemoryStressSmokes/: dedicated runtime aggregator (`MemoryStressSmokes_main.cpp`) for aggressive/long-running memory stress smokes.
- Abi/ModuleSmoke.vcxproj: ABI module loader executable (`ModuleSmoke.exe`) that validates loadable module wiring.
- BenchRunner/: benchmark executable target (`D-Engine-BenchRunner.exe`).
- Math/: targeted math coverage and convention checks.

## Running
- Build the solution (e.g., `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64`) to compile header-only and smoke helpers.
- Run `x64\\Debug\\AllSmokes.exe` (or Release) for aggregate smoke coverage.
- Run `x64\\Debug\\MemoryStressSmokes.exe` (or Release) for extended memory stress coverage.
- Run `x64\\Debug\\ModuleSmoke.exe` (or Release) for ABI module smoke coverage.
- Run `x64\\Release\\D-Engine-BenchRunner.exe` for benchmark coverage.
- For focused memory perf runs:
  - `x64\\Release\\D-Engine-BenchRunner.exe --memory-only --memory-matrix --cpu-info`
  - `python tools/bench_sweep.py --memory-only --memory-matrix --strict-stability`
  - `python tools/memory_bench_sweep.py --strict-stability --stabilize --compare-baseline`
  - Capture baseline candidates (safe default): `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both`
  - Promote baselines (core + memory) after review: `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both -Promote`
  - Memory baseline reference: `bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json`
  - Compare latest memory sweep to baseline:
    `python tools/bench_compare.py bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json <current_memory_bench_json>`

## Memory Coverage
- Runtime memory smokes executed by `AllSmokes`: `ArenaAllocator`, `FrameAllocator`, `StackAllocator`, `SmallObjectAllocator`, `LoggerOnly`, `GuardAllocatorAlignment`, `AllocatorAdapter`, `FrameScope`, `MemorySystem`, `NewDelete`, `OOMPolicy`, `PageAllocator`, `PoolAllocator`, `SmallObjectTLSBins`, `TrackingAllocator`.
- Extended memory stress smokes executed by `MemoryStressSmokes`: `SmallObjectThreadStress` (TLS bins off/on), `SmallObjectFragmentationLongRun`, `MemoryOOMAlignmentExtremes`.
- Build-only memory checks compiled with `D-Engine.vcxproj`: `Test_MemoryRuntimeOverrides_BuildOnly.cpp`, `SelfContain/SmallObjectAllocator_header_only.cpp`, and the memory self-contain/header-order tests under `tests/SelfContain/`.
- Bench memory performance coverage is provided by `BenchRunner` (`ArenaAllocReset`, `FrameAllocReset`, `PoolAllocFreeFixed`, `SmallObjectAllocFreeSmall`, `TrackingOverheadSmallAlloc`).
