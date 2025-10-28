# Benchmarks

## Release Baseline (2025-10-27)
Vector(reserved)=3.357 ns | Arena(64B)=7.971 ns | Arena(8x64B)=56.302 ns | SmallObject TLS(64B)=24.886 ns | Default(64B)=52.486 ns | Tracking(64B)=199.636 ns

## Notes
- flat_map::find stays ahead of std::map::find up to roughly N=64; insert_or_assign wins for flat_map when N <= 16, while std::map overtakes insert timings for N >= 32 and find near N ~ 128.
