# PR Validation

This document defines the minimum checks expected before merging to `main`.

## Required gates
- `python tools/policy_lint.py --strict --modules`
- `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64 /m`
- `msbuild D-Engine.sln /p:Configuration=Release /p:Platform=x64 /m`
- `x64\Release\AllSmokes.exe`
- `x64\Release\MemoryStressSmokes.exe`
- `x64\Release\ModuleSmoke.exe`
- `tools/run_all_gates.ps1 -RequireBench`
- Optional when Rust module changes are touched:
  - `tools/run_all_gates.ps1 -RequireBench -RustModule`

## Perf and leak rules
- Core bench compare must pass against:
  - `bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json`
- Memory bench compare must pass against:
  - `bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json`
- Leak markers are forbidden in smoke and bench logs:
  - `=== MEMORY LEAKS DETECTED ===`
  - `TOTAL LEAKS:`

## Safe GitHub workflow
- Use pull requests for all changes to `main`.
- Use signed commits and verify signature status in GitHub.
- Do not bypass protections except for emergency recovery.
- If a bypass was used, open a follow-up PR that restores normal policy.

## Baseline maintenance
- Update baselines only after a deliberate perf review.
- Keep historical snapshots in `artifacts/bench/` for traceability.
- Safe baseline capture command (no overwrite):
  - `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both`
- Baseline promotion command (explicit overwrite):
  - `powershell -ExecutionPolicy Bypass -File tools/bench_update_baseline.ps1 -Mode both -Promote`
- When updating a baseline, include:
  - command used
  - machine context
  - reason for baseline promotion
