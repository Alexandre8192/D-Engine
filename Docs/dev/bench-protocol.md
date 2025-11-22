# Bench Protocol

## Purpose
- Capture deterministic allocator micro-benchmarks with explicit overrides for unstable scenarios.
- Surface environment controls (power, affinity, timer) alongside timing metrics for auditability.
- Document variance targets so Release bench outputs remain comparable over time.

## Bench Harness Procedure
1. `StabilizeCpu()` elevates the process priority and records the baseline affinity mask.
2. `StabilizeThread()` is invoked for every bench worker (main thread included):
   - `SetThreadIdealProcessor`/`SetThreadAffinityMask` pick distinct logical cores.
   - `SetThreadPriorityBoost(..., TRUE)` disables Windows' priority boosting.
   - A one-time `[Thread] idx=...` log captures processor id, affinity mask and priority.
3. Scenario overrides are applied before the timed loop:
   - `DefaultAllocator` 64B (aligned/unaligned) → `opsPerIter *= 8`, warmups ≥ 2, `maxRepeat ≥ 12`, no early-stop.
   - `TrackingAllocator` 64B → `opsPerIter *= 8`, warmups = 3, `maxRepeat = 15`, no early-stop.
   - `SmallObject/MT4-TLS` → `opsPerIter *= 4`, warmups = 3, `maxRepeat = 15`, no early-stop.
   - `Vector PushPop (no reserve)` → `opsPerIter *= 4` (variance reduction only).
4. Every measured pass logs `[Sample] name opsPerIter=... repeats=... warmups=... earlyStopUsed=...` before the summary line.
5. Multi-thread small-object passes merge per-thread telemetry (affinity OR, priority max) back into the scenario metadata.

## Variance Targets
- Default target: RSD ≤ 3% (enforced when `--target-rsd` is active).
- `SmallObject/MT4-TLS LocalFree T4`: RSD ≤ 5% is tolerated; exceeding 3% still emits a warning, but JSON consumers can gate with the relaxed threshold.
- Bench driver continues to emit `[WARN]` when the ceiling is breached after the configured `maxRepeat`.

## JSON Contract (Schema v1)
The bench driver emits `bench-runner-<sha>-<timestamp>-windows-x64-msvc.bench.json` using the
`dng.bench` schema version 1. The top-level layout is:

```json
{
   "schema": { "name": "dng.bench", "version": 1 },
   "suite": "bench-runner",
   "timestampUtc": "20251029T193625Z",
   "git": { "sha": "a1b2c3d", "dirty": false },
   "run": { "warmupDefault": 1, "repeatMin": 3, "repeatMax": 3, "targetRsd": 3.000 },
   "machine": {
      "osVersion": "Windows 11.0.22635",
      "cpu": {
         "vendor": "GenuineIntel",
         "name": "12th Gen Intel(R) Core(TM) i9-12900K",
         "physicalCores": 8,
         "logicalCores": 16,
         "availableCores": 16,
         "baseMHz": 3200,
         "boostMHz": 5200
      }
   },
   "build": {
      "type": "Release",
      "compiler": "MSVC 193999999",
      "optFlags": "/O2",
      "lto": "OFF",
      "arch": "x64",
      "dirtyTree": false
   },
   "process": {
      "powerPlan": "Ultimate Performance",
      "priorityClass": "HIGH",
      "timerKind": "QPC",
      "cpuAffinity": "0xFFFF",
      "systemAffinity": "0xFFFF",
      "mainThread": {
         "mask": "0x0001",
         "priority": "HIGHEST"
      }
   },
   "scenarios": [ ... ]
}
```

Each scenario entry contains:

- `scenarioId`: 64-bit FNV-1a hash of `<metricName>#<threadCount>` (hex string with leading zeros).
- `name`: descriptive scenario label (JSON-escaped).
- `unit`: currently always `ns/op`.
- `stats`:
   - `median`: primary value reported by BenchDriver (nanoseconds per operation).
   - `mad`: median absolute deviation (robust dispersion measure).
   - `mean`, `stddev`, `min`, `max`: full descriptive statistics.
   - `rsd`: relative standard deviation in percent.
   - `samples`: number of measured repeats contributing to the stats.
- `meta`:
   - `warmups`: manual warm-ups executed before timing.
   - `repeats`: number of measured runs aggregated in `stats`.
   - `opsPerIter`: iteration count actually used inside `DNG_BENCH` after auto-scaling.
   - `threads`: worker threads engaged by the scenario.
   - `affinityMask`: OR of thread affinities (hex).
   - `priority`: resolved thread priority label.
   - `oversubscribed`: `true` when `threads` exceeds `machine.cpu.availableCores`.
   - `earlyStopUsed`: `true` when the early-stop gate concluded the run.
- Optional `alloc` object:
   - `bytesPerOp`: average bytes transferred per operation (when reported).
   - `allocsPerOp`: average allocation count per operation (when reported).

The `git.dirty` and `build.dirtyTree` flags record whether the working tree contained local
modifications at run time. Consumers should prefer `scenarioId` for stable joins and keep the
human-readable `name` for display only.

## Environment Checklist
- Use a fixed-performance power plan (or document the active plan recorded in `env.powerPlan`).
- Ensure Defender, indexing and update tasks are quiescent; re-run noisy samples when RSD still exceeds the documented ceilings.
- Keep the bench runner in a warm system state (no thermal throttling) and avoid background workloads on the cores reserved by the harness.

## Notes
- `tracking_vector` scenarios share element type, iteration shape, and reserve policy with their `vector` counterparts; remaining deltas stem from TrackingAllocator instrumentation overhead.
- Multi-thread timing includes thread creation/destruction by design; this matches the previous harness behaviour while making core pinning explicit.
