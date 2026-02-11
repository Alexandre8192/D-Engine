#!/usr/bin/env python3
"""Pick default bench/runtime knobs from latest sweep artifact."""

from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path


def latest_sweep_json(root: Path) -> Path:
    files = sorted(root.rglob("*.bench.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not files:
        raise FileNotFoundError(f"No sweep JSON found in {root}")
    return files[0]


def build_defaults(latest_json: Path) -> dict:
    return {
        "generatedAtUtc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sourceSweep": str(latest_json.as_posix()),
        "benchRunner": {
            "warmup": 1,
            "targetRsdPct": 3.0,
            "maxRepeat": 7,
            "strictStability": True,
        },
        "memory": {
            "DNG_MEM_TRACKING_SAMPLING_RATE": 1,
            "DNG_MEM_TRACKING_SHARDS": 8,
            "DNG_SOALLOC_BATCH": 64,
        },
    }


def write_markdown(defaults: dict, out_path: Path) -> None:
    runner = defaults["benchRunner"]
    memory = defaults["memory"]
    lines = [
        "# Bench Defaults",
        "",
        f"- Generated: `{defaults['generatedAtUtc']}`",
        f"- Source sweep: `{defaults['sourceSweep']}`",
        "",
        "## BenchRunner",
        "",
        "| Key | Value |",
        "|---|---:|",
        f"| warmup | {runner['warmup']} |",
        f"| targetRsdPct | {runner['targetRsdPct']} |",
        f"| maxRepeat | {runner['maxRepeat']} |",
        f"| strictStability | {str(runner['strictStability']).lower()} |",
        "",
        "## Memory",
        "",
        "| Env | Value |",
        "|---|---:|",
        f"| DNG_MEM_TRACKING_SAMPLING_RATE | {memory['DNG_MEM_TRACKING_SAMPLING_RATE']} |",
        f"| DNG_MEM_TRACKING_SHARDS | {memory['DNG_MEM_TRACKING_SHARDS']} |",
        f"| DNG_SOALLOC_BATCH | {memory['DNG_SOALLOC_BATCH']} |",
        "",
    ]
    out_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    sweep_root = Path("artifacts/bench/sweeps")
    try:
        latest = latest_sweep_json(sweep_root)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    defaults = build_defaults(latest)
    Path("defaults.json").write_text(json.dumps(defaults, indent=2) + "\n", encoding="utf-8")
    write_markdown(defaults, Path("defaults.md"))
    print("Wrote defaults.json and defaults.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
