 # Validation PR: Bench Stability & Runtime Defaults

## Objectif
Verrouiller les contrôles de stabilité BenchRunner et valider la variance ≤ ±3% sur deux runs consécutifs en CI, avec zéro hausse bytes/allocs.

## Changements
- **BenchRunner stabilization**: HIGH priority + pin to first CPU when `--cpu-info` enabled
  - Console affiche: `[CPU] logical=8 affinity=0x1 priority=HIGH`
  - RSD early-stop avec `--target-rsd 3 --max-repeat 7`
  - Stability summaries: `n=X median=Y mean=Z RSD=W% min=A max=B`
- **Runtime defaults (API -> env -> macros)**: sampling=1, shards=8, batch=64
  - MemorySystem logs effective values + source at init
  - Sampling >1 clamped to 1 avec warning (prévu vNext)
- **Bench CI workflow** (`.github/workflows/bench-ci.yml`):
  - Build Release|x64, run bench 2× avec flags
  - Compare ns/op (variance ≤ ±3%), check bytes/allocs (no increase)
  - Upload artifacts (JSONs + MD report) avec NOTICE des defaults
- **Docs**: `Docs/Memory.md` – defaults, precedence, clamping, CI expectations
- **MemoryConfig.hpp**: header mis à jour avec Purpose/Contract/Notes
- **Sweep tools**: `bench_sweep.py`, `bench_pick_defaults.py`, `defaults.json/md`

## Validation locale (déjà effectuée)
```powershell
# Build Release|x64 : OK
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" D-Engine.sln -m -p:Configuration=Release -p:Platform=x64

# Run with stabilization : OK
& .\x64\Release\D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 7 --cpu-info
# [CPU] logical=8 affinity=0x1 priority=HIGH
# [I][Memory] Tracking sampling rate=1 (source=env)
# [I][Memory] Tracking shard count=8 (source=env)
# [I][Memory] SmallObject TLS batch=64 (source=env)
# n=3 median=3.406 mean=3.418 RSD=0.709% min=3.397 max=3.452
# ...
# JSON exported to artifacts/bench/ with value=median, min/max/mean/stddev present
```

## CI attendu (2 runs)
- Build : Release|x64 sur windows-latest
- Run 1 : `D-Engine-BenchRunner.exe --warmup 1 --target-rsd 3 --max-repeat 7 --cpu-info`
- Run 2 : idem
- Compare : |Δ ns/op| ≤ 3% pour toutes les métriques
- Check : bytes/allocs identiques (no increase)
- Upload : `artifacts/bench/**` + `bench_report.md` avec NOTICE

## Merge & Tag
Si CI vert :
1. **Merge** la PR vers main
2. **Tag** : `git tag -a v0.9-Memory -m "Memory subsystem stability controls and runtime defaults"`
3. **Push tag** : `git push origin v0.9-Memory`

## Baseline v2 (optionnel)
Si les métriques Release sont meilleures que v1, promouvoir le dernier JSON :
```powershell
# Comparer avec baseline actuelle
$latest = Get-Content artifacts/bench/current-default/bench-runner-*.bench.json | ConvertFrom-Json
# Si ns/op ↓ et bytes/allocs stable -> copier comme bench/baselines/v2-release.bench.json
```

---
**NOTICE** : Effective defaults applied at runtime (API -> env -> macros). Sampling>1 is currently clamped to 1.
