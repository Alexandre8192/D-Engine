# Benchmarks

## Release Baseline (2025-10-27)
Vector(reserved)=3.357 ns | Arena(64B)=7.971 ns | Arena(8x64B)=56.302 ns | SmallObject TLS(64B)=24.886 ns | Default(64B)=52.486 ns | Tracking(64B)=199.636 ns

## Notes
- flat_map::find stays ahead of std::map::find up to roughly N=64; insert_or_assign wins for flat_map when N <= 16, while std::map overtakes insert timings for N >= 32 and find near N ~ 128.

## Stable-run options (BenchRunner)

- --warmup N (default 0): Perform N warm-up runs to prime caches and JIT-like effects. Warmups are excluded from metrics.
- --repeat M (default 1): Repeat each scenario M times, then compute per-metric statistics across repeats.
	- The JSON keeps backward-compatible fields and sets `value` to the MEDIAN(ns/op).
	- Also includes optional fields: `min`, `max`, `mean`, `stddev` (in ns/op).
	- `bytesPerOp` and `allocsPerOp` remain single values; a WARN is printed if they differ across repeats.

Recommended CI invocation:
- `--warmup 1 --repeat 3` (used by bench-ci and bench-nightly) to reduce run-to-run variance on Windows runners to < 5%.
