# Memory Baseline Snapshot

- commit: `3818f5a`
- date: `2026-02-11`
- source-json: `artifacts/bench/sweeps/memory-baseline-20260211-124933/bench-1770810573.bench.json`
- benchmark-count: `19`
- ok: `15`
- unstable: `4`
- warmup: `1`
- maxRepeat: `20`
- targetRsdPct: `3.0`
- iterations: `20000000`
- memoryOnly: `True`
- memoryMatrix: `True`

| Benchmark | Status | ns/op | rsd% | bytes/op | allocs/op | Reason |
|---|---:|---:|---:|---:|---:|---|
| arena_alloc_reset | unstable | 75.509764 | 4.869 | 0.000000 | 0.000000 | target RSD not reached |
| frame_alloc_reset | ok | 71.447422 | 0.432 | 0.000000 | 0.000000 |  |
| pool_alloc_free_fixed | ok | 152.840371 | 0.679 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_small | unstable | 1327.153476 | 4.679 | 0.000000 | 0.000000 | target RSD not reached |
| tracking_overhead_small_alloc | ok | 4440.176986 | 1.032 | 1024.000000 | 32.000000 |  |
| malloc_free_64 | unstable | 881.926241 | 6.973 | 0.000000 | 0.000000 | target RSD not reached |
| malloc_free_256 | ok | 533.151694 | 0.285 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_16b | ok | 145.323620 | 2.979 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_256b | ok | 43.453431 | 2.429 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_64b_align64 | ok | 76.036961 | 0.592 | 0.000000 | 0.000000 |  |
| frame_alloc_reset_16b | ok | 144.969353 | 0.183 | 0.000000 | 0.000000 |  |
| frame_alloc_reset_256b | unstable | 41.683413 | 4.328 | 0.000000 | 0.000000 | target RSD not reached |
| frame_alloc_reset_64b_align64 | ok | 77.497994 | 2.851 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_16b | ok | 1315.595691 | 0.185 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_64b | ok | 1359.681669 | 0.291 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_256b | ok | 671.460651 | 0.725 | 0.000000 | 0.000000 |  |
| tracking_overhead_alloc_16b | ok | 4295.720540 | 0.407 | 512.000000 | 32.000000 |  |
| tracking_overhead_alloc_64b | ok | 4200.519123 | 0.252 | 2048.000000 | 32.000000 |  |
| tracking_overhead_alloc_256b | ok | 2269.532867 | 0.008 | 4096.000000 | 16.000000 |  |
