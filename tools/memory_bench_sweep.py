#!/usr/bin/env python3
"""Run a memory-focused BenchRunner sweep and emit a Markdown report."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run memory-focused BenchRunner sweep")
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--target-rsd", type=float, default=3.0)
    parser.add_argument("--iterations", type=int, default=20000000)
    parser.add_argument("--bench-filter", type=str, default="")
    parser.add_argument("--emit-md", type=Path, default=None)
    parser.add_argument("--strict-stability", action="store_true")
    return parser.parse_args()


def find_repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def main() -> int:
    args = parse_args()
    repo = find_repo_root()
    sweep_script = repo / "tools" / "bench_sweep.py"
    if not sweep_script.exists():
        print(f"bench_sweep.py not found: {sweep_script}", file=sys.stderr)
        return 1

    command = [
        sys.executable,
        str(sweep_script),
        "--warmup",
        str(max(args.warmup, 0)),
        "--repeat",
        str(max(args.repeat, 1)),
        "--target-rsd",
        str(max(args.target_rsd, 0.0)),
        "--iterations",
        str(max(args.iterations, 1)),
        "--memory-only",
        "--memory-matrix",
    ]
    if args.bench_filter:
        command.extend(["--bench-filter", args.bench_filter])
    if args.emit_md is not None:
        command.extend(["--emit-md", str(args.emit_md)])
    if args.strict_stability:
        command.append("--strict-stability")

    process = subprocess.run(command, cwd=repo, check=False)
    return process.returncode


if __name__ == "__main__":
    sys.exit(main())
