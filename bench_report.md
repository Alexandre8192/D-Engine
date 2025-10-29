| Metric | Baseline | Current | Δns/op | Δ% | Status |
|---|---:|---:|---:|---:|:---:|
| Arena Allocate/Rewind (64B) | 9.788 ns/op | 9.788 ns/op | 0.000 ns/op | 0.00% | OK |
| Arena Bulk Allocate/Rewind (8x64B) | 79.740 ns/op | 79.740 ns/op | 0.000 ns/op | 0.00% | OK |
| DefaultAllocator Alloc/Free 64B | 54.090 ns/op | 54.090 ns/op | 0.000 ns/op | 0.00% | OK |
| TrackingAllocator Alloc/Free 64B | 254.982 ns/op | 254.982 ns/op | 0.000 ns/op | 0.00% | OK |
| Vector PushPop (no reserve) | 3.203 ns/op | 3.203 ns/op | 0.000 ns/op | 0.00% | OK |
| tracking_vector PushPop (no reserve) | 3.206 ns/op | 3.206 ns/op | 0.000 ns/op | 0.00% | OK |
| tracking_vector PushPop (reserved) | 3.206 ns/op | 3.206 ns/op | 0.000 ns/op | 0.00% | OK |