# Bench Sweep Report

- JSON: `artifacts\bench\sweeps\sweep-20260212-111052\bench-1770894652.bench.json`
- memoryOnly: `True`
- memoryMatrix: `True`
- benchFilter: ``

| Benchmark | Status | ns/op | rsd% | bytes/op | allocs/op | Reason |
|---|---:|---:|---:|---:|---:|---|
| arena_alloc_reset | ok | 76.101218 | 7.449 | 0.000000 | 0.000000 |  |
| frame_alloc_reset | ok | 75.004086 | 7.394 | 0.000000 | 0.000000 |  |
| pool_alloc_free_fixed | ok | 158.461052 | 3.046 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_small | unstable | 1566.041382 | 21.805 | 0.000000 | 0.000000 | target RSD not reached |
| tracking_overhead_small_alloc | ok | 4301.401671 | 0.585 | 1024.000000 | 32.000000 |  |
| malloc_free_64 | ok | 1064.036025 | 5.876 | 0.000000 | 0.000000 |  |
| malloc_free_256 | ok | 588.426663 | 0.470 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_16b | ok | 142.704106 | 3.799 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_256b | ok | 42.932616 | 7.515 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_64b_align64 | unstable | 83.423255 | 17.120 | 0.000000 | 0.000000 | target RSD not reached |
| frame_alloc_reset_16b | ok | 155.361443 | 1.168 | 0.000000 | 0.000000 |  |
| frame_alloc_reset_256b | ok | 38.722771 | 0.410 | 0.000000 | 0.000000 |  |
| frame_alloc_reset_64b_align64 | ok | 73.027266 | 0.840 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_16b | ok | 1693.953418 | 0.691 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_64b | ok | 1379.182793 | 7.476 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_256b | ok | 685.554134 | 4.227 | 0.000000 | 0.000000 |  |
| tracking_overhead_alloc_16b | ok | 4158.964130 | 7.479 | 512.000000 | 32.000000 |  |
| tracking_overhead_alloc_64b | ok | 4193.819096 | 2.762 | 2048.000000 | 32.000000 |  |
| tracking_overhead_alloc_256b | ok | 2221.236211 | 7.006 | 4096.000000 | 16.000000 |  |

## Memory Benchmarks
| Benchmark | Status | ns/op | rsd% | bytes/op | allocs/op | Reason |
|---|---:|---:|---:|---:|---:|---|
| arena_alloc_reset | ok | 76.101218 | 7.449 | 0.000000 | 0.000000 |  |
| frame_alloc_reset | ok | 75.004086 | 7.394 | 0.000000 | 0.000000 |  |
| pool_alloc_free_fixed | ok | 158.461052 | 3.046 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_small | unstable | 1566.041382 | 21.805 | 0.000000 | 0.000000 | target RSD not reached |
| tracking_overhead_small_alloc | ok | 4301.401671 | 0.585 | 1024.000000 | 32.000000 |  |
| malloc_free_64 | ok | 1064.036025 | 5.876 | 0.000000 | 0.000000 |  |
| malloc_free_256 | ok | 588.426663 | 0.470 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_16b | ok | 142.704106 | 3.799 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_256b | ok | 42.932616 | 7.515 | 0.000000 | 0.000000 |  |
| arena_alloc_reset_64b_align64 | unstable | 83.423255 | 17.120 | 0.000000 | 0.000000 | target RSD not reached |
| frame_alloc_reset_16b | ok | 155.361443 | 1.168 | 0.000000 | 0.000000 |  |
| frame_alloc_reset_256b | ok | 38.722771 | 0.410 | 0.000000 | 0.000000 |  |
| frame_alloc_reset_64b_align64 | ok | 73.027266 | 0.840 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_16b | ok | 1693.953418 | 0.691 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_64b | ok | 1379.182793 | 7.476 | 0.000000 | 0.000000 |  |
| small_object_alloc_free_256b | ok | 685.554134 | 4.227 | 0.000000 | 0.000000 |  |
| tracking_overhead_alloc_16b | ok | 4158.964130 | 7.479 | 512.000000 | 32.000000 |  |
| tracking_overhead_alloc_64b | ok | 4193.819096 | 2.762 | 2048.000000 | 32.000000 |  |
| tracking_overhead_alloc_256b | ok | 2221.236211 | 7.006 | 4096.000000 | 16.000000 |  |

