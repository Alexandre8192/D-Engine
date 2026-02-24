# D-Engine - Agent Notes

Single source of truth for policies and roadmap:

- `D-Engine_Handbook.md`

This file is intentionally small to avoid duplicating rules.

## Quick commands

Build (MSVC):

- `msbuild D-Engine.sln /p:Configuration=Debug /p:Platform=x64 /m`
- `msbuild D-Engine.sln /p:Configuration=Release /p:Platform=x64 /m`

Run local gates:

- `powershell -ExecutionPolicy Bypass -File tools/run_all_gates.ps1`

Run smokes:

- `x64\Debug\AllSmokes.exe`
- `x64\Release\AllSmokes.exe`
- `x64\Release\MemoryStressSmokes.exe`

Run benchmarks:

- `x64\Release\D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 20 --cpu-info`
