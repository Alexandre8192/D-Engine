#!/usr/bin/env python3
"""Run a lightweight local bench sweep and emit a Markdown summary."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run BenchRunner sweep")
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--target-rsd", type=float, default=3.0)
    parser.add_argument("--emit-md", type=Path, default=None)
    parser.add_argument("--strict-stability", action="store_true")
    return parser.parse_args()


def find_bench_exe() -> Path:
    candidates = [
        Path("x64/Release/D-Engine-BenchRunner.exe"),
        Path("x64/Debug/D-Engine-BenchRunner.exe"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("BenchRunner executable not found. Build D-Engine-BenchRunner first.")


def latest_bench_json(root: Path) -> Path:
    files = sorted(root.rglob("*.bench.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not files:
        raise FileNotFoundError(f"No bench JSON found in {root}")
    return files[0]


def format_md(payload: dict, json_path: Path) -> str:
    lines: List[str] = []
    lines.append("# Bench Sweep Report")
    lines.append("")
    lines.append(f"- JSON: `{json_path}`")
    lines.append("")
    lines.append("| Benchmark | Status | ns/op | rsd% | bytes/op | allocs/op | Reason |")
    lines.append("|---|---:|---:|---:|---:|---:|---|")
    for item in payload.get("benchmarks", []):
        if not isinstance(item, dict):
            continue
        name = item.get("name", "")
        status = item.get("status", "ok")
        value = item.get("value", 0.0)
        rsd = item.get("rsdPct", 0.0)
        bytes_per_op = item.get("bytesPerOp", -1.0)
        allocs_per_op = item.get("allocsPerOp", -1.0)
        reason = item.get("reason", "")
        lines.append(
            f"| {name} | {status} | {float(value):.6f} | {float(rsd):.3f} | "
            f"{float(bytes_per_op):.6f} | {float(allocs_per_op):.6f} | {reason} |"
        )
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()
    exe = find_bench_exe()

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    out_dir = Path("artifacts/bench/sweeps") / f"sweep-{timestamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(exe),
        "--warmup",
        str(max(args.warmup, 0)),
        "--target-rsd",
        str(max(args.target_rsd, 0.0)),
        "--max-repeat",
        str(max(args.repeat, 1)),
        "--cpu-info",
    ]
    if args.strict_stability:
        cmd.append("--strict-stability")

    env = dict(os.environ)
    env["DNG_BENCH_OUT"] = str(out_dir)

    print("Running:", " ".join(cmd))
    process = subprocess.run(cmd, env=env, check=False)
    if process.returncode != 0:
        print(f"BenchRunner failed with exit code {process.returncode}", file=sys.stderr)
        return process.returncode

    json_path = latest_bench_json(out_dir)
    payload = json.loads(json_path.read_text(encoding="utf-8"))
    markdown = format_md(payload, json_path)
    print(markdown)

    if args.emit_md is not None:
        args.emit_md.write_text(markdown, encoding="utf-8")

    return 0


if __name__ == "__main__":
    sys.exit(main())
