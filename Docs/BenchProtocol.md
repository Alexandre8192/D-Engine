# Bench Protocol (Current M0)

## Purpose
- Document how BenchRunner is executed in local gates and CI.
- Keep performance comparisons reproducible enough for regression detection.
- Describe the JSON payload emitted by the current benchmark harness.

## Execution Model
BenchRunner (`tests/BenchRunner/BenchRunner_main.cpp`) runs a small fixed suite and exposes:
- `--warmup N`
- `--target-rsd P`
- `--max-repeat M`
- `--iterations K`

For each scenario, the runner:
1. executes warmup runs,
2. measures repeated batches,
3. computes `ns/op`,
4. stops early when `RSD <= target-rsd` or when `max-repeat` is reached.

## Affinity and Priority
The runner itself does not currently pin affinity or set process priority.

Stabilization is applied by the caller in gates/CI:
- `tools/run_all_gates.ps1` launches BenchRunner via
  `cmd /c start /wait /affinity 1 /high ...`
- `.github/workflows/bench-ci.yml` uses the same pattern.

## Recommended Invocation
- `x64\Release\D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 7`

## JSON Output (M0)
BenchRunner writes `artifacts/bench/bench-<epoch-seconds>.bench.json`:

```json
{
  "benchmarks": [
    {
      "name": "baseline_loop",
      "value": 2.085922,
      "rsdPct": 5.424463,
      "bytesPerOp": 0.0,
      "allocsPerOp": 0.0
    }
  ],
  "metadata": {
    "note": "BenchRunner M0",
    "unit": "ns/op"
  }
}
```

Notes:
- `value` is the measured `ns/op` aggregate used by CI comparisons.
- `bytesPerOp` and `allocsPerOp` are scenario metadata used by `tools/bench_compare.py`.
- Baseline comparisons in CI use `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`.
