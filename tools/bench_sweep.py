#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_sweep.py

Purpose: Run BenchRunner across a small grid of tuning knobs to produce param-swept metrics.
- Knobs (via environment):
  * DNG_MEM_TRACKING_SAMPLING_RATE (e.g., 1, 4, 8)
  * DNG_MEM_TRACKING_SHARDS (e.g., 1, 8)
  * DNG_SOALLOC_BATCH (e.g., 32, 64, 128)
- BenchRunner names include knob tags; outputs go to artifacts/bench/sweeps/<combo>/.
- Optionally emits a Markdown summary table per combo via --emit-md.

Notes:
- On Windows, the script uses `cmd /c start /wait /affinity 1 /high` to pin CPU and priority per run.
- Requires a built BenchRunner at x64/<CONFIGURATION>/D-Engine-BenchRunner.exe.
- No external dependencies beyond the Python stdlib.
"""
from __future__ import annotations
import argparse
import json
import math
import os
import sys
import subprocess
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Tuple

DEFAULT_SAMPLING = "1,4,8"
DEFAULT_SHARDS = "1,8"
DEFAULT_BATCH = "32,64,128"
DEFAULT_METRIC = "TrackingAllocator Alloc/Free 64B"


@dataclass
class ComboResult:
    sampling: int
    shards: int
    batch: int
    combo_key: str
    json_path: Optional[Path]
    metrics: Dict[str, Dict[str, object]]


def parse_grid(arg: Optional[str], default: str) -> List[int]:
    value = arg if arg else default
    parts = [p.strip() for p in value.split(',') if p.strip()]
    out: List[int] = []
    for p in parts:
        try:
            out.append(int(p))
        except ValueError:
            raise ValueError(f"Invalid grid element '{p}' in '{value}'") from None
    if not out:
        raise ValueError(f"Grid '{value}' produced no values")
    return sorted(set(out))


def base_name(metric_name: str) -> str:
    bracket = metric_name.find(' [')
    return metric_name[:bracket] if bracket != -1 else metric_name


def run_once(exe: Path, out_dir: Path, sampling: int, shards: int, batch: int, warmup: int, repeat: int) -> int:
    env = os.environ.copy()
    env['DNG_BENCH_OUT'] = str(out_dir)
    env['DNG_MEM_TRACKING_SAMPLING_RATE'] = str(sampling)
    env['DNG_MEM_TRACKING_SHARDS'] = str(shards)
    env['DNG_SOALLOC_BATCH'] = str(batch)

    # Ensure output directory exists
    out_dir.mkdir(parents=True, exist_ok=True)

    # Build command
    cmd = [str(exe), '--warmup', str(warmup), '--repeat', str(repeat)]

    print(f"[sweep] Running: sampling={sampling} shards={shards} batch={batch} -> {out_dir}")

    if os.name == 'nt':
        # Pin prior to start for stability
        full_cmd = ['cmd', '/c', 'start', '/wait', '/affinity', '1', '/high', ''] + cmd
        return subprocess.call(full_cmd, env=env)
    else:
        return subprocess.call(cmd, env=env)


def find_latest_json(dir_path: Path) -> Path | None:
    cand = sorted(dir_path.glob('**/*.bench.json'), key=lambda p: p.stat().st_mtime, reverse=True)
    return cand[0] if cand else None


def load_metrics(json_path: Path) -> Dict[str, Dict[str, object]]:
    if not json_path or not json_path.exists():
        return {}
    with json_path.open('r', encoding='utf-8') as fh:
        data = json.load(fh)
    metrics = data.get('metrics', []) or []
    out: Dict[str, Dict[str, object]] = {}
    for entry in metrics:
        name = entry.get('name')
        if isinstance(name, str):
            out[name] = entry
    return out


def find_metric(metrics: Dict[str, Dict[str, object]], target_base: str) -> Optional[Dict[str, object]]:
    for name, entry in metrics.items():
        if base_name(name) == target_base:
            return entry
    return None


def format_float(value: Optional[float], fallback: str = "n/a", precision: int = 3) -> str:
    if value is None:
        return fallback
    return f"{value:.{precision}f}"


def analyze_runs(results: List[ComboResult], metric_filter: str, md_path: Optional[Path]) -> None:
    if not results:
        if md_path:
            md_path.write_text("No runs executed.", encoding='utf-8')
        return

    # Baseline: smallest tuple (sampling, shards, batch)
    results_sorted = sorted(results, key=lambda r: (r.sampling, r.shards, r.batch))
    baseline = results_sorted[0]
    baseline_metric = find_metric(baseline.metrics, metric_filter)
    if baseline_metric is None:
        msg = f"Baseline combo {baseline.combo_key} missing metric '{metric_filter}'"
        if md_path:
            md_path.write_text(msg, encoding='utf-8')
        print(f"WARN: {msg}", file=sys.stderr)
        return

    baseline_value = float(baseline_metric.get('value', 0.0))
    baseline_bytes = baseline_metric.get('bytesPerOp')
    baseline_allocs = baseline_metric.get('allocsPerOp')

    # Collect baseline reference for secondary metrics
    baseline_secondary: Dict[str, float] = {}
    other_interest = [
        "tracking_vector PushPop (no reserve)",
        "Arena Allocate/Rewind (64B)",
    ]
    for entry in baseline.metrics.values():
        key = base_name(entry.get('name', ''))
        if key in other_interest:
            baseline_secondary[key] = float(entry.get('value', 0.0))

    rows = []
    best_pick = None

    metric_is_tracking = metric_filter.lower().startswith('tracking')
    rel_thr = 15.0 if metric_is_tracking else 10.0
    abs_thr = 12.0 if metric_is_tracking else 5.0

    for res in results_sorted:
        metric_entry = find_metric(res.metrics, metric_filter)
        if metric_entry is None:
            continue

        median = float(metric_entry.get('value', 0.0))
        delta_abs = median - baseline_value
        if baseline_value != 0:
            delta_pct = (delta_abs / baseline_value) * 100.0
        else:
            delta_pct = math.inf if delta_abs > 0 else (-math.inf if delta_abs < 0 else 0.0)

        bytes_curr = metric_entry.get('bytesPerOp')
        allocs_curr = metric_entry.get('allocsPerOp')

        status = "OK"
        bytes_reg = False
        allocs_reg = False

        if (isinstance(bytes_curr, (int, float)) and isinstance(baseline_bytes, (int, float))
                and bytes_curr > baseline_bytes + 1e-9):
            status = "REG(bytes)"
            bytes_reg = True
        elif (isinstance(allocs_curr, (int, float)) and isinstance(baseline_allocs, (int, float))
              and allocs_curr > baseline_allocs + 1e-9):
            status = "REG(allocs)"
            allocs_reg = True
        elif delta_pct > rel_thr and delta_abs > abs_thr:
            status = "REG(ns)"

        # Secondary metrics degradation check (>3% is disqualifying for best pick)
        secondary_ok = True
        secondary_deltas: List[Tuple[str, float]] = []
        for key, base_val in baseline_secondary.items():
            candidate_entry = find_metric(res.metrics, key)
            if candidate_entry is None:
                continue
            curr_val = float(candidate_entry.get('value', 0.0))
            if base_val != 0:
                delta_sec = ((curr_val - base_val) / base_val) * 100.0
            else:
                delta_sec = math.inf if curr_val > base_val else (-math.inf if curr_val < base_val else 0.0)
            secondary_deltas.append((key, delta_sec))
            if delta_sec > 3.0:
                secondary_ok = False

        rows.append({
            'sampling': res.sampling,
            'shards': res.shards,
            'batch': res.batch,
            'median': median,
            'delta_pct': delta_pct,
            'delta_abs': delta_abs,
            'bytes': bytes_curr if isinstance(bytes_curr, (int, float)) else None,
            'allocs': allocs_curr if isinstance(allocs_curr, (int, float)) else None,
            'status': status,
            'secondary': secondary_deltas,
            'bytes_reg': bytes_reg,
            'allocs_reg': allocs_reg,
            'secondary_ok': secondary_ok,
        })

    rows.sort(key=lambda r: r['median'])

    if md_path:
        lines: List[str] = []
        lines.append(f"## Metric: {metric_filter}")
        lines.append("")
        lines.append("| sampling | shards | batch | ns/op (median) | Δ% vs baseline | bytes/op | allocs/op | status |")
        lines.append("|---:|---:|---:|---:|---:|---:|---:|:---:|")
        for row in rows:
            delta_pct_str = format_float(row['delta_pct']) + "%"
            lines.append(
                f"| {row['sampling']} | {row['shards']} | {row['batch']} | "
                f"{format_float(row['median'])} | {delta_pct_str} | "
                f"{format_float(row['bytes'])} | {format_float(row['allocs'])} | {row['status']} |")

        # Select best pick
        for row in rows:
            if row['status'] != "OK":
                continue
            if not row['secondary_ok']:
                continue
            best_pick = row
            break

        lines.append("")
        lines.append("### Best Pick")
        if best_pick:
            lines.append(
                f"- sampling={best_pick['sampling']}, shards={best_pick['shards']}, batch={best_pick['batch']} "
                f"→ {format_float(best_pick['median'])} ns/op (Δ={format_float(best_pick['delta_pct'])}%)")
            for name, delta in best_pick['secondary']:
                lines.append(f"  - {name}: Δ={format_float(delta)}%")
        else:
            lines.append("- No combination satisfies the constraints (bytes/allocs stable and secondary metrics within +3%).")

        md_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description='Run param sweep for BenchRunner.')
    ap.add_argument('--grid-sampling', help='Comma-separated sampling grid (default: 1,4,8)')
    ap.add_argument('--grid-shards', help='Comma-separated shard grid (default: 1,8)')
    ap.add_argument('--grid-batch', help='Comma-separated SmallObject batch grid (default: 32,64,128)')
    ap.add_argument('--warmup', type=int, default=1)
    ap.add_argument('--repeat', type=int, default=3)
    ap.add_argument('--metric', default=DEFAULT_METRIC, help='Metric base name to analyze (default: TrackingAllocator Alloc/Free 64B)')
    ap.add_argument('--emit-md', dest='emit_md', help='Write a Markdown summary table to this path')
    args = ap.parse_args(argv)

    try:
        sampling_list = parse_grid(args.grid_sampling, DEFAULT_SAMPLING)
        shards_list = parse_grid(args.grid_shards, DEFAULT_SHARDS)
        batch_list = parse_grid(args.grid_batch, DEFAULT_BATCH)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    cfg = os.environ.get('CONFIGURATION', 'Release')
    plat = os.environ.get('PLATFORM', 'x64')
    exe = Path(plat) / cfg / 'D-Engine-BenchRunner.exe'
    if not exe.exists():
        print(f"ERROR: BenchRunner not found: {exe}", file=sys.stderr)
        return 1

    root = Path('artifacts') / 'bench' / 'sweeps'

    results: List[ComboResult] = []
    for s in sampling_list:
        for h in shards_list:
            for b in batch_list:
                combo = f"s{s}-h{h}-b{b}"
                out_dir = root / combo
                code = run_once(exe, out_dir, s, h, b, args.warmup, args.repeat)
                if code != 0:
                    print(f"WARN: Run failed for {combo} with exit code {code}", file=sys.stderr)
                json_path = find_latest_json(out_dir)
                metrics = load_metrics(json_path) if json_path else {}
                results.append(ComboResult(s, h, b, combo, json_path, metrics))

    if args.emit_md:
        analyze_runs(results, args.metric, Path(args.emit_md))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
