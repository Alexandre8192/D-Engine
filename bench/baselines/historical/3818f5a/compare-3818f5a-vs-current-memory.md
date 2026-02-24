# Bench Compare Report

## Comparison
| Benchmark | Status | Baseline ns/op | Current ns/op | Delta ns | Limit ns | Verdict |
|---|---:|---:|---:|---:|---:|---|
| arena_alloc_reset | ok | 76.101218 | 75.756506 | -0.344712 | 76101.218000 | PASS |
| arena_alloc_reset_16b | ok | 142.704106 | 136.070807 | -6.633299 | 142704.106000 | PASS |
| arena_alloc_reset_256b | ok | 42.932616 | 41.260387 | -1.672229 | 42932.616000 | PASS |
| arena_alloc_reset_64b_align64 | ok | 83.423255 | 76.161791 | -7.261464 | 83423.255000 | PASS |
| frame_alloc_reset | ok | 75.004086 | 71.495145 | -3.508941 | 75004.086000 | PASS |
| frame_alloc_reset_16b | ok | 155.361443 | 138.742159 | -16.619284 | 155361.443000 | PASS |
| frame_alloc_reset_256b | ok | 38.722771 | 39.845505 | 1.122734 | 38722.771000 | PASS |
| frame_alloc_reset_64b_align64 | ok | 73.027266 | 74.373740 | 1.346474 | 73027.266000 | PASS |
| malloc_free_256 | ok | 588.426663 | 479.601178 | -108.825485 | 588426.663000 | PASS |
| malloc_free_64 | ok | 1064.036025 | 791.638788 | -272.397237 | 1064036.025000 | PASS |
| pool_alloc_free_fixed | ok | 158.461052 | 150.533329 | -7.927723 | 158461.052000 | PASS |
| small_object_alloc_free_16b | ok | 1693.953418 | 1668.673999 | -25.279419 | 1693953.418000 | PASS |
| small_object_alloc_free_256b | ok | 685.554134 | 643.890159 | -41.663975 | 685554.134000 | PASS |
| small_object_alloc_free_64b | ok | 1379.182793 | 1276.904545 | -102.278248 | 1379182.793000 | PASS |
| small_object_alloc_free_small | ok | 1566.041382 | 1248.943045 | -317.098337 | 1566041.382000 | PASS |
| tracking_overhead_alloc_16b | ok | 4158.964130 | 3953.486872 | -205.477258 | 4158964.130000 | PASS |
| tracking_overhead_alloc_256b | ok | 2221.236211 | 2158.102289 | -63.133922 | 2221236.211000 | PASS |
| tracking_overhead_alloc_64b | ok | 4193.819096 | 3958.061355 | -235.757741 | 4193819.096000 | PASS |
| tracking_overhead_small_alloc | ok | 4301.401671 | 3972.264595 | -329.137076 | 4301401.671000 | PASS |

Result: PASS
