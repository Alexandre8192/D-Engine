#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_compare.py

Purpose: Compare benchmark JSON outputs (current vs baseline) and detect regressions.
- Supports units: ns/op (relative+absolute thresholds), bytes/op and allocs/op (strict increase).
- Tracking* metrics (by name prefix) use relaxed thresholds for ns/op.
- Thresholds are configurable via environment variables.
- Can emit a Markdown summary table via --emit-md.

Contract:
- Input: two JSON files with schema { metrics: [ { name, unit, value } ] }.
- Output: exit code 0 when within thresholds; 1 if regressions detected. Writes optional Markdown file.
- No external dependencies beyond Python stdlib.

Notes:
- Missing or new metrics are WARN-only and do not cause failure.
- Absolute thresholds are evaluated only on positive deltas (i.e., slower/larger is bad).
"""
from __future__ import annotations
import argparse
import json
import math
import os
import sys
from typing import Dict, List, Tuple, Optional

Metric = Dict[str, object]


def _env_float(name: str, default: float) -> float:
    v = os.environ.get(name)
    if v is None:
        return default
    try:
        return float(v)
    except Exception:
        return default


def _is_tracking(name: str) -> bool:
    return name.lower().startswith("tracking")


def _load_metrics(path: str) -> Dict[str, Metric]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    metrics = data.get("metrics", []) or []
    out: Dict[str, Metric] = {}
    for m in metrics:
        name = m.get("name")
        if isinstance(name, str):
            out[name] = m
    return out


def _fmt(v: float, unit: str) -> str:
    return f"{v:.3f} {unit}".strip()


def compare(baseline_path: str, current_path: str) -> Tuple[bool, List[str], List[Tuple[str, str]]]:
    """
    Returns (ok, warns, rows) where rows are tuples of (name, status)
    and prints a human-readable summary to stdout.
    """
    base = _load_metrics(baseline_path)
    curr = _load_metrics(current_path)

    # Thresholds
    ns_rel = _env_float("PERF_THRESHOLD_PCT", 10.0)
    ns_abs = _env_float("PERF_ABS_NS", 5.0)
    ns_rel_track = _env_float("PERF_THRESHOLD_PCT_TRACKING", 15.0)
    ns_abs_track = _env_float("PERF_ABS_NS_TRACKING", 12.0)
    by_abs = _env_float("BYTES_OP_MAX_ABS", 0.0)
    by_rel = _env_float("BYTES_OP_MAX_REL_PCT", 0.0)
    al_abs = _env_float("ALLOCS_OP_MAX_ABS", 0.0)
    al_rel = _env_float("ALLOCS_OP_MAX_REL_PCT", 0.0)

    names_base = set(base.keys())
    names_curr = set(curr.keys())
    warns: List[str] = []
    ok = True

    # Warn about missing/new metrics
    for n in sorted(names_base - names_curr):
        warns.append(f"WARN: Metric missing in current: {n}")
    for n in sorted(names_curr - names_base):
        warns.append(f"WARN: New metric in current (no baseline): {n}")

    def pct(delta: float, base_val: float) -> float:
        if base_val == 0:
            if delta > 0:
                return math.inf
            elif delta < 0:
                return -math.inf
            return 0.0
        return (delta / base_val) * 100.0

    # Prepare results for markdown
    rows: List[Tuple[str, str]] = []  # (name, status)

    for name in sorted(names_base & names_curr):
        b = base[name]
        c = curr[name]
        unit_b = str(b.get("unit", ""))
        unit_c = str(c.get("unit", ""))
        unit = unit_c or unit_b
        try:
            bval = float(b.get("value", 0))
            cval = float(c.get("value", 0))
        except Exception:
            # Skip malformed values but warn
            warns.append(f"WARN: Non-numeric values for metric {name}; skipping")
            continue

        d = cval - bval
        rp = pct(d, bval)
        status = "OK"
        reason = ""

        # ns/op thresholds (primary)
        if unit == "ns/op":
            if _is_tracking(name):
                rel_thr = ns_rel_track
                abs_thr = ns_abs_track
            else:
                rel_thr = ns_rel
                abs_thr = ns_abs
            if d > abs_thr or rp > rel_thr:
                ok = False
                status = "REG"
                reason = f"ns/op delta {d:.3f}ns (rel {rp:.2f}%); thresholds abs>{abs_thr}ns or rel>{rel_thr}%"
        else:
            # Unknown unit: do not enforce ns/op rule
            reason = "(unit not enforced)"

        # bytes/op strict increase checks if present in schema
        try:
            b_bytes = float(b.get("bytesPerOp", 0)) if ("bytesPerOp" in b) else None  # type: ignore
            c_bytes = float(c.get("bytesPerOp", 0)) if ("bytesPerOp" in c) else None  # type: ignore
        except Exception:
            b_bytes = c_bytes = None
            warns.append(f"WARN: Non-numeric bytesPerOp for metric {name}; skipping bytes/op check")

        if b_bytes is not None and c_bytes is not None:
            d_bytes = c_bytes - b_bytes
            rp_bytes = (d_bytes / b_bytes * 100.0) if (b_bytes and b_bytes != 0) else (math.inf if d_bytes > 0 else (-math.inf if d_bytes < 0 else 0.0))
            if c_bytes > b_bytes and (d_bytes > by_abs or rp_bytes > by_rel):
                ok = False
                if status == "OK":
                    status = "REG"
                reason = (reason + " ").strip() + f"[bytes/op ↑ {d_bytes:.3f} ({rp_bytes:.2f}%)]"
        # allocs/op strict increase checks if present
        try:
            b_allocs = float(b.get("allocsPerOp", 0)) if ("allocsPerOp" in b) else None  # type: ignore
            c_allocs = float(c.get("allocsPerOp", 0)) if ("allocsPerOp" in c) else None  # type: ignore
        except Exception:
            b_allocs = c_allocs = None
            warns.append(f"WARN: Non-numeric allocsPerOp for metric {name}; skipping allocs/op check")

        if b_allocs is not None and c_allocs is not None:
            d_allocs = c_allocs - b_allocs
            rp_allocs = (d_allocs / b_allocs * 100.0) if (b_allocs and b_allocs != 0) else (math.inf if d_allocs > 0 else (-math.inf if d_allocs < 0 else 0.0))
            if c_allocs > b_allocs and (d_allocs > al_abs or rp_allocs > al_rel):
                ok = False
                if status == "OK":
                    status = "REG"
                reason = (reason + " ").strip() + f"[allocs/op ↑ {d_allocs:.3f} ({rp_allocs:.2f}%)]"

        # Print per-metric summary (focus on ns/op; append bytes/allocs notes if any)
        print(f"{name}: baseline={_fmt(bval, unit)} -> current={_fmt(cval, unit)} ; Δ={_fmt(d, unit)} ({rp:.2f}%) [{status}] {reason}")
        rows.append((name, status))

    for w in warns:
        print(w, file=sys.stderr)

    return ok, warns, rows


def emit_md(baseline_path: str, current_path: str, out_path: str) -> None:
    base = _load_metrics(baseline_path)
    curr = _load_metrics(current_path)

    names_all = sorted(set(base.keys()) | set(curr.keys()))

    def get(m: Dict[str, Metric], n: str) -> Tuple[Optional[float], str]:
        if n not in m:
            return None, ""
        v = m[n].get("value")
        u = str(m[n].get("unit", ""))
        try:
            return float(v), u
        except Exception:
            return None, u

    lines: List[str] = []
    lines.append("| Metric | Baseline | Current | Δns/op | Δ% | Status |")
    lines.append("|---|---:|---:|---:|---:|:---:|")

    for name in names_all:
        bv, bu = get(base, name)
        cv, cu = get(curr, name)
        unit = cu or bu
        status = "N/A"
        if (bv is not None) and (cv is not None):
            d = cv - bv
            rp = (d / bv * 100.0) if (bv and bv != 0) else (math.inf if d > 0 else (-math.inf if d < 0 else 0.0))
            # Quick status mirror of compare() rules (ns/op primary)
            ns_rel = _env_float("PERF_THRESHOLD_PCT", 10.0)
            ns_abs = _env_float("PERF_ABS_NS", 5.0)
            ns_rel_track = _env_float("PERF_THRESHOLD_PCT_TRACKING", 15.0)
            ns_abs_track = _env_float("PERF_ABS_NS_TRACKING", 12.0)
            rel_thr = ns_rel_track if name.lower().startswith("tracking") else ns_rel
            abs_thr = ns_abs_track if name.lower().startswith("tracking") else ns_abs
            status = "REG" if (d > abs_thr or rp > rel_thr) else "OK"

            # Also reflect bytes/allocs strict checks in status label
            # Pull bytes/allocs if present
            # Note: we don't display their deltas in the table to keep it short.
            #      If they regress, we annotate the status.
            # These fields may be missing; ignore if so.
            b_bytes = base.get(name, {}).get("bytesPerOp") if name in base else None  # type: ignore
            c_bytes = curr.get(name, {}).get("bytesPerOp") if name in curr else None  # type: ignore
            b_allocs = base.get(name, {}).get("allocsPerOp") if name in base else None  # type: ignore
            c_allocs = curr.get(name, {}).get("allocsPerOp") if name in curr else None  # type: ignore
            try:
                if b_bytes is not None and c_bytes is not None:
                    db = float(c_bytes) - float(b_bytes)
                    rb = (db / float(b_bytes) * 100.0) if (b_bytes and float(b_bytes) != 0) else (math.inf if db > 0 else (-math.inf if db < 0 else 0.0))
                    by_abs = _env_float("BYTES_OP_MAX_ABS", 0.0)
                    by_rel = _env_float("BYTES_OP_MAX_REL_PCT", 0.0)
                    if float(c_bytes) > float(b_bytes) and (db > by_abs or rb > by_rel):
                        status = "REG (bytes)" if status == "OK" else status
                if b_allocs is not None and c_allocs is not None:
                    da = float(c_allocs) - float(b_allocs)
                    ra = (da / float(b_allocs) * 100.0) if (b_allocs and float(b_allocs) != 0) else (math.inf if da > 0 else (-math.inf if da < 0 else 0.0))
                    al_abs = _env_float("ALLOCS_OP_MAX_ABS", 0.0)
                    al_rel = _env_float("ALLOCS_OP_MAX_REL_PCT", 0.0)
                    if float(c_allocs) > float(b_allocs) and (da > al_abs or ra > al_rel):
                        status = "REG (allocs)" if status == "OK" else status
            except Exception:
                pass

            d_str = f"{d:.3f} ns/op"
            rp_str = f"{rp:.2f}%"
            b_str = f"{bv:.3f} ns/op"
            c_str = f"{cv:.3f} ns/op"
        else:
            b_str = "(missing)"
            c_str = "(new)" if bv is None and cv is not None else ("(missing)" if cv is None and bv is not None else "(n/a)")
            d_str = "-"
            rp_str = "-"
            status = "OK"  # WARN-only for schema drift
        lines.append(f"| {name} | {b_str} | {c_str} | {d_str} | {rp_str} | {status} |")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Compare bench JSON vs baseline with thresholds.")
    ap.add_argument("baseline", help="Path to baseline .json")
    ap.add_argument("current", help="Path to current .json")
    ap.add_argument("--emit-md", dest="emit_md", help="Write a Markdown summary table to this path.")
    args = ap.parse_args(argv)

    ok, warns, _rows = compare(args.baseline, args.current)

    if args.emit_md:
        try:
            emit_md(args.baseline, args.current, args.emit_md)
        except Exception as e:
            print(f"WARN: Failed to emit markdown: {e}", file=sys.stderr)

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
