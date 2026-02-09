# Benchmarks

## Release Baseline (2025-10-27)
Vector(reserved)=3.357 ns | Arena(64B)=7.971 ns | Arena(8x64B)=56.302 ns | SmallObject TLS(64B)=24.886 ns | Default(64B)=52.486 ns | Tracking(64B)=199.636 ns

## Notes
- flat_map::find stays ahead of std::map::find up to roughly N=64; insert_or_assign wins for flat_map when N <= 16, while std::map overtakes insert timings for N >= 32 and find near N ~ 128.

## Stable-run options (BenchRunner)

- --warmup N (default 1): Warm-up runs before measurement.
- --target-rsd P (default 3.0): Stop early once sample RSD is below threshold.
- --max-repeat M (default 15): Maximum measured repeats per scenario.
- --iterations K (default 20000000): Base iteration budget (bench-specific scaling still applies).

Current BenchRunner JSON (M0) emits:
- `benchmarks[]` with `name`, `value` (ns/op), `rsdPct`, `bytesPerOp`, `allocsPerOp`
- `metadata` with `note` and `unit`

Recommended CI invocation:
- `--warmup 1 --target-rsd 3 --max-repeat 7`
