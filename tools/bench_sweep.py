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
    parser.add_argument("--iterations", type=int, default=None)
    parser.add_argument("--memory-only", action="store_true")
    parser.add_argument("--memory-matrix", action="store_true")
    parser.add_argument("--bench-filter", type=str, default="")
    parser.add_argument("--emit-md", type=Path, default=None)
    parser.add_argument("--strict-stability", action="store_true")
    parser.add_argument("--stabilize", action="store_true")
    parser.add_argument("--affinity-mask", type=str, default="1")
    parser.add_argument("--normal-priority", action="store_true")
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


def run_bench(
    exe: Path,
    args: List[str],
    env: dict[str, str],
    stabilize: bool,
    affinity_mask: str,
    normal_priority: bool,
) -> int:
    if stabilize and os.name == "nt":
        arg_text = subprocess.list2cmdline(args)
        priority = "" if normal_priority else " /high"
        cmd_text = (
            "start /wait /affinity "
            + affinity_mask
            + priority
            + ' "" "'
            + str(exe)
            + '" '
            + arg_text
        )
        print("Running (stabilized): cmd /c", cmd_text)
        process = subprocess.run(["cmd", "/c", cmd_text], env=env, check=False)
        return process.returncode

    cmd = [str(exe), *args]
    print("Running:", " ".join(cmd))
    process = subprocess.run(cmd, env=env, check=False)
    return process.returncode


def format_md(payload: dict, json_path: Path) -> str:
    def is_memory_bench(name: str) -> bool:
        key = name.lower()
        return any(
            token in key
            for token in (
                "arena",
                "frame",
                "pool",
                "small_object",
                "tracking",
                "malloc",
                "alloc",
                "memory",
            )
        )

    lines: List[str] = []
    lines.append("# Bench Sweep Report")
    lines.append("")
    lines.append(f"- JSON: `{json_path}`")
    metadata = payload.get("metadata", {})
    if isinstance(metadata, dict):
        if "memoryOnly" in metadata:
            lines.append(f"- memoryOnly: `{metadata.get('memoryOnly')}`")
        if "memoryMatrix" in metadata:
            lines.append(f"- memoryMatrix: `{metadata.get('memoryMatrix')}`")
        if "benchFilter" in metadata:
            lines.append(f"- benchFilter: `{metadata.get('benchFilter')}`")
    lines.append("")
    lines.append("| Benchmark | Status | ns/op | rsd% | bytes/op | allocs/op | Reason |")
    lines.append("|---|---:|---:|---:|---:|---:|---|")
    memory_rows: List[str] = []
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
        row = (
            f"| {name} | {status} | {float(value):.6f} | {float(rsd):.3f} | "
            f"{float(bytes_per_op):.6f} | {float(allocs_per_op):.6f} | {reason} |"
        )
        lines.append(row)
        if is_memory_bench(str(name)):
            memory_rows.append(row)
    if memory_rows:
        lines.append("")
        lines.append("## Memory Benchmarks")
        lines.append("| Benchmark | Status | ns/op | rsd% | bytes/op | allocs/op | Reason |")
        lines.append("|---|---:|---:|---:|---:|---:|---|")
        lines.extend(memory_rows)
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()
    exe = find_bench_exe()

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    out_dir = Path("artifacts/bench/sweeps") / f"sweep-{timestamp}"
    out_dir.mkdir(parents=True, exist_ok=True)

    bench_args = [
        "--warmup",
        str(max(args.warmup, 0)),
        "--target-rsd",
        str(max(args.target_rsd, 0.0)),
        "--max-repeat",
        str(max(args.repeat, 1)),
        "--cpu-info",
    ]
    if args.strict_stability:
        bench_args.append("--strict-stability")
    if args.iterations is not None:
        bench_args.extend(["--iterations", str(max(args.iterations, 1))])
    if args.memory_only:
        bench_args.append("--memory-only")
    if args.memory_matrix:
        bench_args.append("--memory-matrix")
    if args.bench_filter:
        bench_args.extend(["--bench-filter", args.bench_filter])

    env = dict(os.environ)
    env["DNG_BENCH_OUT"] = str(out_dir)

    exit_code = run_bench(
        exe=exe,
        args=bench_args,
        env=env,
        stabilize=args.stabilize,
        affinity_mask=args.affinity_mask,
        normal_priority=args.normal_priority,
    )
    if exit_code != 0:
        print(f"BenchRunner failed with exit code {exit_code}", file=sys.stderr)
        return exit_code

    json_path = latest_bench_json(out_dir)
    payload = json.loads(json_path.read_text(encoding="utf-8"))
    markdown = format_md(payload, json_path)
    print(markdown)

    if args.emit_md is not None:
        args.emit_md.write_text(markdown, encoding="utf-8")

    return 0


if __name__ == "__main__":
    sys.exit(main())
