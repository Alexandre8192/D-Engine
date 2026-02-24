# Benchmarks

## Release Baseline
Canonical baseline JSON lives in:
- `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`
- `bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json`

Historical memory snapshot (commit `3818f5a`) is stored in:
- `bench/baselines/historical/3818f5a/bench-runner-memory-release-windows-x64-msvc-3818f5a.baseline.json`
- `bench/baselines/historical/3818f5a/3818f5a-memory-baseline-report.md`

## Notes
- BenchRunner now emits per-benchmark status (`ok` / `skipped` / `unstable` / `error`) so platform-dependent scenarios are explicit.
- CI strict mode uses `--strict-stability` to fail when any benchmark stays unstable after max repeats.

## Current scenarios
- Core microbenchmarks: `baseline_loop`, `vec3_dot`, `memcpy_64`
- Allocator-focused: `arena_alloc_reset`, `frame_alloc_reset`, `pool_alloc_free_fixed`,
  `small_object_alloc_free_small`, `tracking_overhead_small_alloc`
- Audio path: `audio_mix_null_1024f_stereo`, `audio_mix_mem_clip_platform_1024f_stereo`,
  `audio_mix_stream_clip_platform_1024f_stereo`

## Stable-run options (BenchRunner v2)

- --warmup N (default 1): Warm-up runs before measurement.
- --target-rsd P (default 3.0): Stop early once sample RSD is below threshold.
- --max-repeat M (default 15): Maximum measured repeats per scenario.
- --repeat M: Alias for `--max-repeat`.
- --iterations K (default 20000000): Base iteration budget (bench-specific scaling still applies).
- --cpu-info: Emit runtime CPU/affinity/priority diagnostics.
- --strict-stability: Return non-zero if any benchmark remains unstable.

Current BenchRunner JSON (schema v2) emits:
- `schemaVersion`
- `benchmarks[]` with `name`, `value` (ns/op), `rsdPct`, `bytesPerOp`, `allocsPerOp`
- `benchmarks[]` status metadata: `status`, `reason`, `repeatsUsed`, `targetRsdPct`
- `summary` aggregate counts (`okCount`, `skippedCount`, `unstableCount`, `errorCount`)
- `metadata` with run options and schema version

Recommended CI invocation:
- Core: `--warmup 1 --target-rsd 3 --max-repeat 20 --cpu-info --strict-stability`
- Memory: `--warmup 2 --target-rsd 8 --max-repeat 24 --cpu-info --memory-only --memory-matrix --strict-stability`

Recommended local baseline workflow:
- Capture candidates (safe default, no overwrite):
  - `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both`
- Promote canonical baselines only after review:
  - `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both -Promote`
