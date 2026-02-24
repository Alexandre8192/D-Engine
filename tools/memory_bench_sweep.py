#!/usr/bin/env python3
"""Run a memory-focused BenchRunner sweep and emit a Markdown report."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run memory-focused BenchRunner sweep")
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--repeat", type=int, default=24)
    parser.add_argument("--target-rsd", type=float, default=8.0)
    parser.add_argument("--iterations", type=int, default=20000000)
    parser.add_argument("--bench-filter", type=str, default="")
    parser.add_argument("--emit-md", type=Path, default=None)
    parser.add_argument("--strict-stability", action="store_true")
    parser.add_argument("--stabilize", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--affinity-mask", type=str, default="1")
    parser.add_argument("--normal-priority", action="store_true")
    parser.add_argument("--compare-baseline", action="store_true")
    parser.add_argument(
        "--baseline",
        type=Path,
        default=Path("bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json"),
    )
    parser.add_argument("--threshold-pct", type=float, default=8.0)
    parser.add_argument("--threshold-pct-tracking", type=float, default=None)
    parser.add_argument(
        "--ignore-benchmark",
        type=str,
        default="small_object_alloc_free_16b,small_object_alloc_free_small",
    )
    return parser.parse_args()


def find_repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def latest_bench_json(root: Path) -> Path:
    files = sorted(root.rglob("*.bench.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not files:
        raise FileNotFoundError(f"No bench JSON found in {root}")
    return files[0]


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
    if args.stabilize:
        command.extend(["--stabilize", "--affinity-mask", args.affinity_mask])
        if args.normal_priority:
            command.append("--normal-priority")
    if args.bench_filter:
        command.extend(["--bench-filter", args.bench_filter])
    if args.emit_md is not None:
        command.extend(["--emit-md", str(args.emit_md)])
    if args.strict_stability:
        command.append("--strict-stability")

    process = subprocess.run(command, cwd=repo, check=False)
    if process.returncode != 0:
        return process.returncode

    if not args.compare_baseline:
        return 0

    if not args.baseline.is_absolute():
        baseline = repo / args.baseline
    else:
        baseline = args.baseline
    if not baseline.exists():
        print(f"Memory baseline not found: {baseline}", file=sys.stderr)
        return 1

    try:
        current = latest_bench_json(repo / "artifacts/bench/sweeps")
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    threshold_tracking = (
        args.threshold_pct if args.threshold_pct_tracking is None else args.threshold_pct_tracking
    )
    compare_script = repo / "tools" / "bench_compare.py"
    compare_cmd = [
        sys.executable,
        str(compare_script),
        str(baseline),
        str(current),
        "--perf-threshold-pct",
        str(args.threshold_pct),
        "--perf-threshold-pct-tracking",
        str(threshold_tracking),
        "--allow-unstable-from-baseline",
    ]
    if args.ignore_benchmark:
        compare_cmd.extend(["--ignore-benchmark", args.ignore_benchmark])

    print("Comparing memory bench output:")
    print(" ", " ".join(compare_cmd))
    compare = subprocess.run(compare_cmd, cwd=repo, check=False)
    return compare.returncode


if __name__ == "__main__":
    sys.exit(main())
