# Bench Protocol (Current)

## Purpose
- Document how BenchRunner is executed in local gates and CI.
- Keep performance comparisons reproducible enough for regression detection.
- Describe the JSON payload emitted by the current benchmark harness.

## Execution Model
BenchRunner (`tests/BenchRunner/BenchRunner_main.cpp`) runs a fixed suite and exposes:
- `--warmup N`
- `--target-rsd P`
- `--max-repeat M`
- `--repeat M` (alias for `--max-repeat`)
- `--iterations K`
- `--cpu-info`
- `--strict-stability`

For each scenario, the runner:
1. executes warmup runs,
2. measures repeated batches,
3. computes `ns/op`,
4. stops early when `RSD <= target-rsd` or when `max-repeat` is reached,
5. marks the scenario as `unstable` when target RSD is still not met.

## Affinity and Priority
The runner itself does not currently pin affinity or set process priority.

Stabilization is applied by the caller in gates/CI:
- `tools/run_all_gates.ps1` launches BenchRunner via
  `cmd /c start /wait /affinity 1 /high ...`
- `.github/workflows/bench-ci.yml` uses the same pattern.

## Recommended Invocation
- `x64\Release\D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 12 --cpu-info --strict-stability`

## JSON Output (Schema v2)
BenchRunner writes `artifacts/bench/bench-<epoch-seconds>.bench.json`:

```json
{
  "schemaVersion": 2,
  "benchmarks": [
    {
      "name": "baseline_loop",
      "value": 2.085922,
      "rsdPct": 5.424463,
      "bytesPerOp": 0.0,
      "allocsPerOp": 0.0,
      "status": "ok",
      "reason": "",
      "repeatsUsed": 4,
      "targetRsdPct": 3.0
    }
  ],
  "summary": {
    "okCount": 6,
    "skippedCount": 0,
    "unstableCount": 0,
    "errorCount": 0
  },
  "metadata": {
    "note": "BenchRunner v2",
    "strictStability": true,
    "schemaVersion": 2,
    "unit": "ns/op"
  }
}
```

Notes:
- `value` is measured `ns/op`.
- `bytesPerOp` and `allocsPerOp` are measured at runtime through `Core/Diagnostics/Bench.hpp`.
- `status` and `reason` make skipped/unavailable scenarios explicit (for example platform audio backends).
- Baseline comparisons in CI use `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`.
