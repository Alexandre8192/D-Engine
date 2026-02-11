# Benchmarks

## Release Baseline
Canonical baseline JSON lives in:
- `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`

## Notes
- BenchRunner now emits per-benchmark status (`ok` / `skipped` / `unstable` / `error`) so platform-dependent scenarios are explicit.
- CI strict mode uses `--strict-stability` to fail when any benchmark stays unstable after max repeats.

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
- `--warmup 1 --target-rsd 3 --max-repeat 12 --cpu-info --strict-stability`
