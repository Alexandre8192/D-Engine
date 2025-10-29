# D-Engine Memory Defaults and Runtime Precedence

Purpose: Document production defaults for memory subsystem knobs and how runtime precedence works for effective values.

Contract: Applies to Release | x64 unless explicitly overridden. Effective values are determined at process startup by the following order: API → environment → macros. Determinism focused; no hidden costs; changes are logged once at MemorySystem initialization.

Notes: Tracking sampling > 1 is currently clamped to 1 by design (vNext will lift this). SmallObject batch is clamped to [1, DNG_SOA_TLS_MAG_CAPACITY]. Tracking shards must be a power-of-two; invalid values fall back to macro defaults.

## Effective defaults (Release)

- Tracking sampling: 1 (DNG_MEM_TRACKING_SAMPLING_RATE)
- Tracking shards: 8 (DNG_MEM_TRACKING_SHARDS)
- SmallObject batch: 64 (DNG_SOALLOC_BATCH)

These align with the 2025‑10‑29 sweep results and provide stable benchmarks while keeping bytes/allocs unchanged.

## Precedence and observability

Order of resolution at runtime:

1) API override via `dng::core::MemoryConfig` passed to `dng::memory::MemorySystem::Init()`
2) Environment variables at process start:
	 - DNG_MEM_TRACKING_SAMPLING_RATE
	 - DNG_MEM_TRACKING_SHARDS
	 - DNG_SOALLOC_BATCH
3) Compile-time macros (see `Source/Core/Memory/MemoryConfig.hpp`)

MemorySystem logs the final effective values and their source once at init:

- "Tracking sampling rate=X (source=api|env|macro)"
- "Tracking shard count=Y (source=api|env|macro)"
- "SmallObject TLS batch=Z (source=api|env|macro)"

## Constraints and clamping

- sampling ≥ 1; values > 1 currently clamp to 1 with a warning (until sampling support lands)
- shards must be power‑of‑two; invalid values fall back to macro default with a warning
- batch in [1, DNG_SOA_TLS_MAG_CAPACITY]; out‑of‑range values clamp with a warning

## Bench CI expectations

- CPU stabilization: process HIGH priority + pin to one logical core to reduce variance
- Warmup + repeats with early stop on target RSD (relative stddev)
- JSON outputs median as `value`; `min/max/mean/stddev` only when repeats > 1
- Two consecutive runs must satisfy:
	- |Δ ns/op| ≤ ±3% for key metrics
	- No increase in `bytesPerOp` or `allocsPerOp`
- Artifacts: both JSON files and a markdown report are uploaded
- NOTICE: "Effective defaults applied at runtime (API → env → macros). Sampling>1 is currently clamped to 1."

## Quick reference

Headers:
- `Source/Core/Memory/MemoryConfig.hpp` – knobs (macros + runtime) and invariants
- `Source/Core/Memory/MemorySystem.hpp` – runtime precedence resolution and logging

Env vars:
- `DNG_MEM_TRACKING_SAMPLING_RATE` (≥ 1)
- `DNG_MEM_TRACKING_SHARDS` (power‑of‑two)
- `DNG_SOALLOC_BATCH` (1..DNG_SOA_TLS_MAG_CAPACITY)
# Memory System Defaults

The release configuration relies on bench-derived knobs to balance tracking diagnostics with production latency. All measurements below come from `artifacts/bench/sweeps/` and the accompanying `defaults.md` rendered on 2025-10-29.

### Production Defaults (Release | x64)

| Knob | Default | Rationale |
|---|---|---|
| `DNG_MEM_TRACKING_SAMPLING_RATE` | 1 | `TrackingAllocator Alloc/Free 64B`: 203.639 ns/op (Δ=-42.909 ns, -17.404% vs `s1-h1-b32` baseline 246.548 ns); bytes/op 64 and allocs/op 1 unchanged. Secondary checks from `tracking_vector PushPop (no reserve)` within -1.561%, `Arena Allocate/Rewind (64B)` improved by -25.282%. |
| `DNG_MEM_TRACKING_SHARDS` | 8 | Same sweep combo (`s1-h8-b64`) kept contention bounded while maintaining byte/alloc parity. Increasing shards beyond 8 did not produce measurable wins and increases allocator footprint; fewer shards regressed TrackingAllocator latency by up to +17%. |
| `DNG_SOALLOC_BATCH` | 64 | `SmallObject 64B`: 26.466 ns/op (Δ=+0.096 ns, +0.364% vs `batch=32` baseline 26.37 ns); bytes/op and allocs/op remained 0. Larger batches showed diminishing returns with elevated variance. |

The baseline JSON used for comparisons lives in `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`. Raw sweep outputs are archived under `artifacts/bench/sweeps/` (see `defaults.md` for the Markdown roll-up).

Nightly validation via `.github/workflows/bench-nightly.yml` reruns the benches with these settings to detect regressions in TrackingAllocator, SmallObjectAllocator, and arena helpers.

> Runtime note: until sampling-backed leak tracking ships, overrides that request `tracking_sampling_rate > 1` are clamped back to `1` with a warning so we preserve deterministic allocation bookkeeping.
