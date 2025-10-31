# D-Engine Memory Defaults and Runtime Precedence

Purpose: Document production defaults for memory subsystem knobs and how runtime precedence works for effective values.

Contract: Applies to Release | x64 unless explicitly overridden. Effective values are determined at process startup by the following order: API → environment → macros. Determinism focused; no hidden costs; changes are logged once at MemorySystem initialization.

Notes: Tracking sampling > 1 is currently clamped to 1 by design (vNext will lift this). SmallObject batch is clamped to [1, DNG_SOA_TLS_MAG_CAPACITY]. Tracking shards must be a power-of-two; invalid values fall back to macro defaults.

Last updated: 2025-10-31

## At a glance

- Effective defaults (Release | x64): tracking sampling=1, tracking shards=8, SmallObject batch=64
- Precedence at startup: API → environment → macros (resolved once by `MemorySystem::Init()` and logged)
- Determinism-first: no hidden costs; sampling > 1 clamps to 1 until sampling support ships

## Table of contents

- [Choosing the right allocator](#choosing-the-right-allocator)
- [Allocator reference](#allocator-reference)
	- [SmallObjectAllocator (Temporary)](#smallobjectallocator-temporary)
	- [FrameScope (MemStack)](#framescope-memstack)
	- [DefaultAllocator (Persistent)](#defaultallocator-persistent)
	- [PoolAllocator (Persistent, fixed shape)](#poolallocator-persistent-fixed-shape)
	- [GuardAllocator (High-safety)](#guardallocator-high-safety)
	- [TrackingAllocator (High-safety diagnostics)](#trackingallocator-high-safety-diagnostics)
	- [Parent allocator fallback (High alignment / large payloads)](#parent-allocator-fallback-high-alignment--large-payloads)
- [Effective defaults (Release)](#effective-defaults-release)
- [Precedence and observability](#precedence-and-observability)
- [Constraints and clamping](#constraints-and-clamping)
- [Bench CI expectations](#bench-ci-expectations)
- [Quick reference](#quick-reference)
- [Memory System Defaults](#memory-system-defaults)

## Choosing the right allocator

| Usage pattern | Recommended allocator(s) | Rationale |
| --- | --- | --- |
| Temporary, transient work (default) | `SmallObjectAllocator` (class-local) + `FrameScope` per thread | Hot-path bump-style flow where `SmallObjectAllocator` covers ≤1024 B classes and `FrameScope` guarantees MemStack rewind at scope exit. |
| Persistent systems / lifetime-controlled heaps | `DefaultAllocator` or `PoolAllocator` | General-purpose fallback (`DefaultAllocator`) or fixed-size pools where object counts are known and reuse dominates. |
| High-safety diagnostics (development builds) | `GuardAllocator`, `TrackingAllocator` | Adds guard pages/redzones and full tracking with deterministic cost; expect higher latency and memory but maximum visibility. |
| Alignments ≥ 32/64 bytes or large payloads | Call parent allocator explicitly | `SmallObjectAllocator` caps at 16-byte natural alignment; larger requirements must bypass to the parent allocator to avoid wasted slab space. |

## Allocator reference

### SmallObjectAllocator (Temporary)

- **Purpose**: Serve deterministic slab-backed allocations for objects up to 1 KiB while sharding contention and offloading bulk frees to `FrameScope` or explicit deallocation.
- **Contract**: Alignment support limited to the class natural alignment (≤16 bytes). All allocations must be freed with identical `(size, alignment)`; cross-thread frees are routed through sharded lists. Per-thread caches honour runtime shard overrides.
- **Notes**: Pair with `FrameScope` to avoid manual rewinds. When a caller requires ≥32-byte alignment, forward the request to the parent allocator instead of forcing a slab path.

### FrameScope (MemStack)

- **Purpose**: RAII guard that acquires the per-thread frame allocator and rewinds on scope exit, enabling burst allocations inside gameplay/render frames.
- **Contract**: Requires `MemorySystem::Init()` to provision per-thread MemStack storage. Thread affinity is per scope; the scope rewinds only when ownership is active. Nested scopes are supported.
- **Notes**: Use this by default for transient allocations; scopes should stay cheap and avoid heap fallbacks. Remote threads must attach via `MemorySystem::OnThreadAttach()` before constructing a scope.

### DefaultAllocator (Persistent)

- **Purpose**: General-purpose allocator used by most engine subsystems when no specialised policy applies.
- **Contract**: Thread-safe according to global memory configuration. Callers must respect `(size, alignment)` symmetry. Preferred for long-lived objects and fallbacks from specialised allocators.
- **Notes**: Acts as the parent for multiple allocator layers (e.g., SmallObject, Tracking). For large alignments or slab misses, delegate here explicitly.

### PoolAllocator (Persistent, fixed shape)

- **Purpose**: Handle fixed-size object pools where lifetime is managed externally and reuse is predictable.
- **Contract**: Callers predefine block size/count. Pool operations are deterministic; freeing returns blocks to the pool without touching the system allocator.
- **Notes**: Best for component pools or ECS storage when the set of active objects is bounded. When pool exhaustion is possible, capture that path in telemetry.

### GuardAllocator (High-safety)

- **Purpose**: Instrument allocations with guard regions and optional poison to detect overruns and use-after-free during development.
- **Contract**: Higher latency and memory usage; not intended for shipping builds. Requires the parent allocator to be tracking-compatible for diagnostics.
- **Notes**: Enable when validating third-party integrations or chasing memory corruption. Expect guard alignment to increase footprint; disable for performance sweeps.

### TrackingAllocator (High-safety diagnostics)

- **Purpose**: Provide monotonic allocation counters and leak detection with optional stack capture.
- **Contract**: Thread-safe when compiled with tracking. `MemorySystem::Init()` resolves sampling/shard overrides; sampling > 1 is currently clamped to 1.
- **Notes**: Wire into bench runs to surface `bytesPerOp` and `allocsPerOp`. Use in dev/test; in production builds leave sampling at defaults to avoid hidden costs.

### Parent allocator fallback (High alignment / large payloads)

- **Purpose**: Service requests that exceed `SmallObjectAllocator` class sizes or alignment guarantees.
- **Contract**: Callers must call the underlying parent allocator (typically `DefaultAllocator`) directly when requesting ≥32/64-byte alignment or sizes >1 KiB.
- **Notes**: Document the fallback in subsystem contracts so callers understand they may incur general-heap contention. Consider dedicated pools if high-alignment requests dominate.

## Effective defaults (Release)

- Tracking sampling: 1 (DNG_MEM_TRACKING_SAMPLING_RATE)
- Tracking shards: 8 (DNG_MEM_TRACKING_SHARDS)
- SmallObject batch: 64 (DNG_SOALLOC_BATCH)

These align with the 2025‑10‑29 sweep results and provide stable benchmarks while keeping bytes/allocs unchanged.

Validated as of 2025‑10‑31; see `artifacts/bench/bench-results-20251031-windows-x64-msvc.json` for the latest nightly results.

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

### Setting env vars (Windows PowerShell)

```powershell
# Session-only overrides (effective for processes launched from this session)
$env:DNG_MEM_TRACKING_SAMPLING_RATE = "1"
$env:DNG_MEM_TRACKING_SHARDS = "8"
$env:DNG_SOALLOC_BATCH = "64"

# Launch your application from the same session
# .\D-Engine.exe   # or your engine/game executable
```

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

## Revision history

- 2025-10-31: Reorganized document (added At a glance, Table of contents, Allocator reference header, Windows PowerShell env-var snippet) and updated validation reference to latest artifacts.
