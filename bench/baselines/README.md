# D-Engine Bench Baselines

Purpose: Version stable, small baseline results to detect regressions. Run outputs go to artifacts/bench/ (ignored by Git).

Contract:
- Only small JSON/CSV baselines are versioned here.
- Runtime results must go under artifacts/bench/ (ignored).
- Naming: {suite}-{gitsha}-{dateUtc}-{platform}.json

Minimal JSON schema:
{
  "suite": "string",
  "gitSha": "string",
  "dateUtc": "YYYY-MM-DDTHH:MM:SSZ",
  "platform": { "os": "string", "cpu": "string", "compiler": "string" },
  "metrics": [
    { "name": "string", "unit": "string", "value": 0.0, "nsPerOp": 0.0 }
  ]
}

Notes:
- DNG_BENCH_OUT env var can override the default output dir (see Core/Diagnostics/Bench.hpp).
- Keep units explicit; prefer ns/op and bytes/sec when relevant.
