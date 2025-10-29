#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_pick_defaults.py

Purpose: Inspect sweep results under artifacts/bench/sweeps/ and propose default knob values
         for memory tracking sampling/shards and SmallObject batch sizing.
Contract:
- Consumes existing *.bench.json files produced by bench_sweep.py.
- Applies the same regression rules used for the sweep report (bytes/allocs must not increase;
  ns/op regressions flagged when Δ%% > 10% and Δns > 5, or 15%/12ns for Tracking* metrics).
- Selects the lowest-latency combination that keeps secondary metrics (tracking_vector, Arena 64B)
  within +3% of the baseline.
- Emits defaults.json and defaults.md alongside summary notes.
Notes:
- Baseline is the lexicographically-smallest grid tuple (sampling, shards, batch).
- JSON schema is unchanged; the script only reads existing fields.
- No external dependencies beyond the Python stdlib.
"""
from __future__ import annotations

import json
import math
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

SWEEP_ROOT = Path('artifacts') / 'bench' / 'sweeps'
DEFAULTS_JSON = Path('defaults.json')
DEFAULTS_MD = Path('defaults.md')

KEY_METRICS = [
    'TrackingAllocator Alloc/Free 64B',
    'SmallObject 64B',
]
SECONDARY_METRICS = [
    'tracking_vector PushPop (no reserve)',
    'Arena Allocate/Rewind (64B)',
]


@dataclass
class Combo:
    sampling: int
    shards: int
    batch: int
    metrics: Dict[str, Dict[str, object]]
    source: Path

    @property
    def key(self) -> Tuple[int, int, int]:
        return (self.sampling, self.shards, self.batch)


def base_name(name: str) -> str:
    idx = name.find(' [')
    return name[:idx] if idx != -1 else name


def parse_combo_from_path(path: Path) -> Optional[Tuple[int, int, int]]:
    # Expect directories named s<sampling>-h<shards>-b<batch>
    for parent in path.parents:
        parts = parent.name.split('-')
        if len(parts) != 3:
            continue
        try:
            if parts[0].startswith('s') and parts[1].startswith('h') and parts[2].startswith('b'):
                s = int(parts[0][1:])
                h = int(parts[1][1:])
                b = int(parts[2][1:])
                return (s, h, b)
        except ValueError:
            continue
    return None


def load_metrics(json_path: Path) -> Dict[str, Dict[str, object]]:
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


def delta_pct(curr: float, base: float) -> float:
    if base == 0:
        if curr == base:
            return 0.0
        return math.inf if curr > base else -math.inf
    return ((curr - base) / base) * 100.0


def evaluate_status(target_base: str,
                    entry: Dict[str, object],
                    baseline_entry: Dict[str, object]) -> Tuple[str, float, float, bool, bool]:
    curr = float(entry.get('value', 0.0))
    base = float(baseline_entry.get('value', 0.0))
    abs_delta = curr - base
    pct_delta = delta_pct(curr, base)

    bytes_curr = entry.get('bytesPerOp')
    bytes_base = baseline_entry.get('bytesPerOp')
    allocs_curr = entry.get('allocsPerOp')
    allocs_base = baseline_entry.get('allocsPerOp')

    bytes_reg = (isinstance(bytes_curr, (int, float)) and isinstance(bytes_base, (int, float))
                 and bytes_curr > bytes_base + 1e-9)
    allocs_reg = (isinstance(allocs_curr, (int, float)) and isinstance(allocs_base, (int, float))
                  and allocs_curr > allocs_base + 1e-9)

    metric_is_tracking = target_base.lower().startswith('tracking')
    rel_thr = 15.0 if metric_is_tracking else 10.0
    abs_thr = 12.0 if metric_is_tracking else 5.0

    if bytes_reg:
        status = 'REG(bytes)'
    elif allocs_reg:
        status = 'REG(allocs)'
    elif pct_delta > rel_thr and abs_delta > abs_thr:
        status = 'REG(ns)'
    else:
        status = 'OK'
    return status, pct_delta, abs_delta, bytes_reg, allocs_reg


def secondary_ok(combo: Combo,
                 baseline_metrics: Dict[str, float]) -> Tuple[bool, Dict[str, float]]:
    deltas: Dict[str, float] = {}
    ok = True
    for name, base_val in baseline_metrics.items():
        entry = find_metric(combo.metrics, name)
        if entry is None:
            continue
        curr = float(entry.get('value', 0.0))
        pct = delta_pct(curr, base_val)
        deltas[name] = pct
        if pct > 3.0:
            ok = False
    return ok, deltas


def discover_combos() -> List[Combo]:
    combos: Dict[Tuple[int, int, int], Combo] = {}
    for json_path in SWEEP_ROOT.glob('**/*.bench.json'):
        combo_tuple = parse_combo_from_path(json_path)
        if combo_tuple is None:
            continue
        metrics = load_metrics(json_path)
        key = combo_tuple
        # Use the latest file for each combo (sorted by mtime ensures newest overrides)
        prev = combos.get(key)
        if prev is None or json_path.stat().st_mtime > prev.source.stat().st_mtime:
            combos[key] = Combo(combo_tuple[0], combo_tuple[1], combo_tuple[2], metrics, json_path)
    return sorted(combos.values(), key=lambda c: c.key)


def pick_defaults(combos: List[Combo]) -> Tuple[Optional[Combo], Dict[str, Dict[str, object]]]:
    if not combos:
        return None, {}

    baseline = combos[0]
    baseline_entries: Dict[str, Dict[str, object]] = {}
    for metric_name, entry in baseline.metrics.items():
        baseline_entries[base_name(metric_name)] = entry

    secondary_baseline: Dict[str, float] = {}
    for sec in SECONDARY_METRICS:
        entry = find_metric(baseline.metrics, sec)
        if entry is not None:
            secondary_baseline[sec] = float(entry.get('value', 0.0))

    best: Optional[Combo] = None
    best_stats: Dict[str, Dict[str, object]] = {}

    for combo in combos:
        metric_stats: Dict[str, Dict[str, object]] = {}
        disqualified = False

        for metric in KEY_METRICS:
            baseline_metric = baseline_entries.get(metric)
            entry = find_metric(combo.metrics, metric)
            if baseline_metric is None or entry is None:
                disqualified = True
                break
            status, pct_delta, abs_delta, bytes_reg, allocs_reg = evaluate_status(metric, entry, baseline_metric)
            metric_stats[metric] = {
                'status': status,
                'pct_delta': pct_delta,
                'abs_delta': abs_delta,
                'value': float(entry.get('value', 0.0)),
                'bytes': entry.get('bytesPerOp'),
                'allocs': entry.get('allocsPerOp'),
                'bytes_reg': bytes_reg,
                'allocs_reg': allocs_reg,
            }
            if status != 'OK':
                disqualified = True
                break

        if disqualified:
            continue

        sec_ok, sec_deltas = secondary_ok(combo, secondary_baseline)
        if not sec_ok:
            continue
        metric_stats['secondary'] = sec_deltas

        if best is None:
            best = combo
            best_stats = metric_stats
            continue

        # Prefer lower Tracking median primarily, then SmallObject, then lexicographic tuple.
        tracking_val = metric_stats['TrackingAllocator Alloc/Free 64B']['value']
        best_tracking = best_stats['TrackingAllocator Alloc/Free 64B']['value']
        if tracking_val < best_tracking - 1e-6:
            best = combo
            best_stats = metric_stats
            continue
        if abs(tracking_val - best_tracking) <= 1e-6:
            small_curr = metric_stats['SmallObject 64B']['value']
            small_best = best_stats['SmallObject 64B']['value']
            if small_curr < small_best - 1e-6:
                best = combo
                best_stats = metric_stats
                continue
            if abs(small_curr - small_best) <= 1e-6 and combo.key < best.key:
                best = combo
                best_stats = metric_stats

    return best, best_stats


def write_outputs(best: Optional[Combo], stats: Dict[str, Dict[str, object]]) -> None:
    if best is None:
        text = "No suitable defaults could be selected (missing data or all combinations regress).\n"
        DEFAULTS_MD.write_text(text, encoding='utf-8')
        DEFAULTS_JSON.write_text('{}\n', encoding='utf-8')
        return

    data = {
        'sampling': best.sampling,
        'shards': best.shards,
        'batch': best.batch,
    }
    DEFAULTS_JSON.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')

    lines: List[str] = []
    lines.append('# Bench Defaults Proposal')
    lines.append('')
    lines.append(f'- sampling = {best.sampling}')
    lines.append(f'- shards   = {best.shards}')
    lines.append(f'- batch    = {best.batch}')
    lines.append('')
    lines.append('## Metric Summary vs Baseline')
    lines.append('')
    lines.append('| Metric | Median ns/op | Δ% | Δns | bytes/op | allocs/op | Status |')
    lines.append('|---|---:|---:|---:|---:|---:|:---:|')
    for metric in KEY_METRICS:
        stat = stats.get(metric, {})
        value = stat.get('value')
        pct = stat.get('pct_delta')
        abs_delta = stat.get('abs_delta')
        bytes_val = stat.get('bytes')
        allocs_val = stat.get('allocs')
        lines.append(
            "| {metric} | {value} | {pct}% | {abs_delta} | {bytes} | {allocs} | {status} |".format(
                metric=metric,
                value=f"{value:.3f}" if isinstance(value, (int, float)) else 'n/a',
                pct=f"{pct:.3f}" if isinstance(pct, (int, float)) else 'n/a',
                abs_delta=f"{abs_delta:.3f}" if isinstance(abs_delta, (int, float)) else 'n/a',
                bytes=f"{bytes_val:.3f}" if isinstance(bytes_val, (int, float)) else 'n/a',
                allocs=f"{allocs_val:.3f}" if isinstance(allocs_val, (int, float)) else 'n/a',
                status=stat.get('status', 'n/a'),
            ))
    lines.append('')
    secondary = stats.get('secondary', {})
    if secondary:
        lines.append('### Secondary Metric Deltas (must stay ≤ +3%)')
        for name, pct in secondary.items():
            lines.append(f'- {name}: Δ={pct:.3f}%')
    lines.append('')
    DEFAULTS_MD.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main() -> int:
    combos = discover_combos()
    if not combos:
        print(f"No sweep JSON files found under {SWEEP_ROOT}", file=sys.stderr)
        return 1

    best, stats = pick_defaults(combos)
    write_outputs(best, stats)

    if best:
        print(f"Selected defaults: sampling={best.sampling}, shards={best.shards}, batch={best.batch}")
    else:
        print("No defaults selected; inspect defaults.md for details.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
