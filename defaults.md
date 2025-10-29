# Bench Defaults Proposal

- sampling = 1
- shards   = 8
- batch    = 64

## Metric Summary vs Baseline

| Metric | Median ns/op | Δ% | Δns | bytes/op | allocs/op | Status |
|---|---:|---:|---:|---:|---:|:---:|
| TrackingAllocator Alloc/Free 64B | 203.639 | -17.404% | -42.909 | 64.000 | 1.000 | OK |
| SmallObject 64B | 26.466 | 0.364% | 0.096 | 0.000 | 0.000 | OK |

### Secondary Metric Deltas (must stay ≤ +3%)
- tracking_vector PushPop (no reserve): Δ=-1.561%
- Arena Allocate/Rewind (64B): Δ=-25.282%

