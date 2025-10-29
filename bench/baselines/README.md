# D-Engine Bench Baselines

Purpose: Version stable benchmark baselines to detect performance regressions in CI. Live run outputs go to `artifacts/bench/` (ignored by Git) and are compared against the versioned baseline.

Contract:
- Only compact JSON baselines are versioned under this folder.
- Runtime outputs MUST go under `artifacts/bench/` (ignored by Git).
- Baseline naming: `bench-runner-release-windows-x64-msvc.baseline.json` (stable name referenced by CI).
- JSON schema is defined by the bench runner; do NOT change field names. Each metric has at least: `{ name, unit, value }` and may include `{ bytesPerOp, allocsPerOp }`.

Baseline update policy:
- Update a baseline ONLY when:
  - A confirmed performance improvement is achieved (intentional optimization), or
  - Intentional benchmark/test changes alter the measurements in a known way.
- Do NOT “fix” regressions by updating baselines. Regressions must be investigated and addressed in code.
- Baseline updates should be included in the same PR that introduces the intentional change, with a short explanation in the commit message.

Comparator thresholds (as enforced by CI):
- ns/op: fail if slower by more than 10% relative OR 5 ns absolute (defaults).
  - For metrics whose names start with `Tracking`, thresholds are 15% and 12 ns.
- bytes/op: zero-tolerance for increases by default (any increase fails).
- allocs/op: zero-tolerance for increases by default (any increase fails).
- All thresholds are configurable via environment variables in CI; see the workflow header comments.

Reproducible runs (local):
- Build `Release|x64`.
- Run the bench runner with affinity and priority pinned (mirrors CI):
  - `cmd /c start /wait /affinity 1 /high "D-Engine-BenchRunner" x64\Release\D-Engine-BenchRunner.exe`
- Outputs go to `artifacts/bench/` by default; override with `DNG_BENCH_OUT`.

Notes:
- `DNG_BENCH_OUT` can override the default output directory (see `Source/Core/Diagnostics/Bench.hpp`).
- Keep metric units explicit (`ns/op`, and optional `bytesPerOp`, `allocsPerOp`).
