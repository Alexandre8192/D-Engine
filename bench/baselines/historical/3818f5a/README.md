# 3818f5a Memory Baseline Snapshot

- Source commit: `3818f5a`
- Captured on: `2026-02-12`
- Capture workspace: `../D-Engine-3818f5a` (detached worktree at `3818f5a`)
- Platform: `Windows x64`
- Build: `Release`
- Command:
  - `python tools/memory_bench_sweep.py --warmup 2 --repeat 24 --target-rsd 8 --emit-md artifacts/bench/baselines/3818f5a-memory-baseline-report.md`
- Output JSON:
  - `bench-runner-memory-release-windows-x64-msvc-3818f5a.baseline.json`
- Output report:
  - `3818f5a-memory-baseline-report.md`

Notes
- A strict-stability run was attempted first with `--strict-stability` and failed due unstable benchmarks.
- The frozen snapshot above uses non-strict mode, preserving unstable statuses in the report/JSON.
