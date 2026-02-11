#!/usr/bin/env python3
"""Compare BenchRunner JSON output against a baseline.

Exit code:
- 0: comparison passed
- 1: regression or invalid data detected
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


@dataclass
class Thresholds:
    perf_rel_pct: float
    perf_abs_ns: float
    tracking_rel_pct: float
    tracking_abs_ns: float
    bytes_abs: float
    bytes_rel_pct: float
    allocs_abs: float
    allocs_rel_pct: float
    fail_on_skip: bool


@dataclass
class BenchEntry:
    name: str
    value: float
    rsd_pct: float
    bytes_per_op: float
    allocs_per_op: float
    status: str
    reason: str


def env_float(name: str, default: float) -> float:
    text = os.environ.get(name)
    if text is None:
        return default
    try:
        return float(text)
    except ValueError:
        return default


def env_bool(name: str, default: bool) -> bool:
    text = os.environ.get(name)
    if text is None:
        return default
    value = text.strip().lower()
    if value in ("1", "true", "yes", "on"):
        return True
    if value in ("0", "false", "no", "off"):
        return False
    return default


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare bench JSON against baseline")
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--emit-md", type=Path, default=None)
    parser.add_argument("--perf-threshold-pct", type=float, default=env_float("PERF_THRESHOLD_PCT", 10.0))
    parser.add_argument("--perf-abs-ns", type=float, default=env_float("PERF_ABS_NS", 5.0))
    parser.add_argument(
        "--perf-threshold-pct-tracking",
        type=float,
        default=env_float("PERF_THRESHOLD_PCT_TRACKING", 15.0),
    )
    parser.add_argument("--perf-abs-ns-tracking", type=float, default=env_float("PERF_ABS_NS_TRACKING", 12.0))
    parser.add_argument("--bytes-op-max-abs", type=float, default=env_float("BYTES_OP_MAX_ABS", 0.0))
    parser.add_argument("--bytes-op-max-rel-pct", type=float, default=env_float("BYTES_OP_MAX_REL_PCT", 0.0))
    parser.add_argument("--allocs-op-max-abs", type=float, default=env_float("ALLOCS_OP_MAX_ABS", 0.0))
    parser.add_argument("--allocs-op-max-rel-pct", type=float, default=env_float("ALLOCS_OP_MAX_REL_PCT", 0.0))
    parser.add_argument("--fail-on-skip", action="store_true", default=env_bool("PERF_FAIL_ON_SKIP", False))
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def to_float(value: object, default: float = math.nan) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    return default


def normalize_entry(raw: dict) -> BenchEntry:
    name = str(raw.get("name", ""))
    status = str(raw.get("status", "ok")).strip().lower() or "ok"
    reason = str(raw.get("reason", ""))
    return BenchEntry(
        name=name,
        value=to_float(raw.get("value")),
        rsd_pct=to_float(raw.get("rsdPct"), 0.0),
        bytes_per_op=to_float(raw.get("bytesPerOp"), math.nan),
        allocs_per_op=to_float(raw.get("allocsPerOp"), math.nan),
        status=status,
        reason=reason,
    )


def map_entries(payload: dict) -> Dict[str, BenchEntry]:
    raw = payload.get("benchmarks", [])
    if not isinstance(raw, list):
        return {}
    mapped: Dict[str, BenchEntry] = {}
    for item in raw:
        if not isinstance(item, dict):
            continue
        entry = normalize_entry(item)
        if entry.name:
            mapped[entry.name] = entry
    return mapped


def classify_threshold(name: str, thresholds: Thresholds) -> Tuple[float, float]:
    lowered = name.lower()
    if "tracking" in lowered:
        return thresholds.tracking_rel_pct, thresholds.tracking_abs_ns
    return thresholds.perf_rel_pct, thresholds.perf_abs_ns


def allowed_delta(base: float, rel_pct: float, abs_value: float) -> float:
    rel = abs(base) * (rel_pct / 100.0)
    return max(rel, abs_value)


def is_measured(value: float) -> bool:
    return math.isfinite(value) and value >= 0.0


def format_float(value: float) -> str:
    if math.isfinite(value):
        return f"{value:.6f}"
    return "nan"


def build_markdown(rows: List[str], failures: List[str], warnings: List[str]) -> str:
    lines: List[str] = []
    lines.append("# Bench Compare Report")
    lines.append("")
    if failures:
        lines.append("## Failures")
        for message in failures:
            lines.append(f"- {message}")
        lines.append("")
    if warnings:
        lines.append("## Warnings")
        for message in warnings:
            lines.append(f"- {message}")
        lines.append("")
    lines.append("## Comparison")
    lines.append("| Benchmark | Status | Baseline ns/op | Current ns/op | Delta ns | Limit ns | Verdict |")
    lines.append("|---|---:|---:|---:|---:|---:|---|")
    lines.extend(rows)
    lines.append("")
    lines.append(f"Result: {'FAIL' if failures else 'PASS'}")
    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()
    thresholds = Thresholds(
        perf_rel_pct=args.perf_threshold_pct,
        perf_abs_ns=args.perf_abs_ns,
        tracking_rel_pct=args.perf_threshold_pct_tracking,
        tracking_abs_ns=args.perf_abs_ns_tracking,
        bytes_abs=args.bytes_op_max_abs,
        bytes_rel_pct=args.bytes_op_max_rel_pct,
        allocs_abs=args.allocs_op_max_abs,
        allocs_rel_pct=args.allocs_op_max_rel_pct,
        fail_on_skip=args.fail_on_skip,
    )

    if not args.baseline.exists():
        print(f"Baseline file not found: {args.baseline}", file=sys.stderr)
        return 1
    if not args.current.exists():
        print(f"Current bench file not found: {args.current}", file=sys.stderr)
        return 1

    baseline_payload = load_json(args.baseline)
    current_payload = load_json(args.current)
    baseline = map_entries(baseline_payload)
    current = map_entries(current_payload)

    failures: List[str] = []
    warnings: List[str] = []
    rows: List[str] = []

    baseline_names = set(baseline.keys())
    current_names = set(current.keys())

    missing = sorted(baseline_names - current_names)
    added = sorted(current_names - baseline_names)
    for name in missing:
        failures.append(f"Missing benchmark in current run: {name}")
    for name in added:
        failures.append(f"Unexpected benchmark not found in baseline: {name}")

    for name in sorted(baseline_names & current_names):
        base = baseline[name]
        cur = current[name]
        verdict = "PASS"

        if cur.status == "error":
            failures.append(f"{name}: benchmark status=error ({cur.reason})")
            verdict = "FAIL"
        elif cur.status == "unstable":
            failures.append(f"{name}: benchmark status=unstable (rsd={cur.rsd_pct:.3f}%)")
            verdict = "FAIL"
        elif cur.status == "skipped":
            message = f"{name}: benchmark status=skipped ({cur.reason})"
            if thresholds.fail_on_skip:
                failures.append(message)
                verdict = "FAIL"
            else:
                warnings.append(message)

        if cur.status == "ok":
            if not math.isfinite(base.value) or not math.isfinite(cur.value):
                failures.append(f"{name}: invalid numeric value (baseline={base.value}, current={cur.value})")
                verdict = "FAIL"
            else:
                rel_pct, abs_ns = classify_threshold(name, thresholds)
                limit_ns = allowed_delta(base.value, rel_pct, abs_ns)
                delta_ns = cur.value - base.value
                if delta_ns > limit_ns:
                    failures.append(
                        f"{name}: ns/op regression {delta_ns:.6f} exceeds limit {limit_ns:.6f} "
                        f"(base={base.value:.6f}, cur={cur.value:.6f})"
                    )
                    verdict = "FAIL"

            if is_measured(base.bytes_per_op) and is_measured(cur.bytes_per_op):
                limit = allowed_delta(base.bytes_per_op, thresholds.bytes_rel_pct, thresholds.bytes_abs)
                delta = cur.bytes_per_op - base.bytes_per_op
                if delta > limit:
                    failures.append(
                        f"{name}: bytes/op increase {delta:.6f} exceeds limit {limit:.6f} "
                        f"(base={base.bytes_per_op:.6f}, cur={cur.bytes_per_op:.6f})"
                    )
                    verdict = "FAIL"

            if is_measured(base.allocs_per_op) and is_measured(cur.allocs_per_op):
                limit = allowed_delta(base.allocs_per_op, thresholds.allocs_rel_pct, thresholds.allocs_abs)
                delta = cur.allocs_per_op - base.allocs_per_op
                if delta > limit:
                    failures.append(
                        f"{name}: allocs/op increase {delta:.6f} exceeds limit {limit:.6f} "
                        f"(base={base.allocs_per_op:.6f}, cur={cur.allocs_per_op:.6f})"
                    )
                    verdict = "FAIL"

        delta_text = "n/a"
        limit_text = "n/a"
        if math.isfinite(base.value) and math.isfinite(cur.value):
            rel_pct, abs_ns = classify_threshold(name, thresholds)
            delta_text = f"{(cur.value - base.value):.6f}"
            limit_text = f"{allowed_delta(base.value, rel_pct, abs_ns):.6f}"

        rows.append(
            f"| {name} | {cur.status} | "
            f"{format_float(base.value)} | "
            f"{format_float(cur.value)} | "
            f"{delta_text} | {limit_text} | {verdict} |"
        )

    markdown = build_markdown(rows, failures, warnings)
    if args.emit_md is not None:
        args.emit_md.write_text(markdown, encoding="utf-8")

    print(markdown)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
