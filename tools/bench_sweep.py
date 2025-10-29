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
import os
import sys
import subprocess
from pathlib import Path
from datetime import datetime, timezone

DEFAULT_SAMPLING = [1, 4, 8]
DEFAULT_SHARDS = [1, 8]
DEFAULT_BATCH = [32, 64, 128]


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
        full_cmd = ['cmd', '/c', 'start', '/wait', '/affinity', '1', '/high', 'D-Engine-BenchRunner'] + cmd
        return subprocess.call(full_cmd, env=env)
    else:
        return subprocess.call(cmd, env=env)


def find_latest_json(dir_path: Path) -> Path | None:
    cand = sorted(dir_path.glob('**/*.bench.json'), key=lambda p: p.stat().st_mtime, reverse=True)
    return cand[0] if cand else None


def emit_md_table(md_path: Path, runs: list[tuple[str, Path]]) -> None:
    lines: list[str] = []
    lines.append('| Combo | JSON | Timestamp UTC |')
    lines.append('|---|---|---|')
    for combo, dirp in runs:
        jf = find_latest_json(dirp)
        ts = 'n/a'
        if jf and jf.exists():
            ts = datetime.fromtimestamp(jf.stat().st_mtime, tz=timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
        lines.append(f"| {combo} | {jf.name if jf else '(none)'} | {ts} |")
    md_path.write_text('\n'.join(lines), encoding='utf-8')


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description='Run param sweep for BenchRunner.')
    ap.add_argument('--sampling', nargs='*', type=int, default=DEFAULT_SAMPLING, help='Sampling rates to sweep')
    ap.add_argument('--shards', nargs='*', type=int, default=DEFAULT_SHARDS, help='Shard counts to sweep')
    ap.add_argument('--batch', nargs='*', type=int, default=DEFAULT_BATCH, help='SmallObject batch sizes to sweep')
    ap.add_argument('--warmup', type=int, default=1)
    ap.add_argument('--repeat', type=int, default=3)
    ap.add_argument('--emit-md', dest='emit_md', help='Write a Markdown summary table to this path')
    args = ap.parse_args(argv)

    cfg = os.environ.get('CONFIGURATION', 'Release')
    plat = os.environ.get('PLATFORM', 'x64')
    exe = Path(plat) / cfg / 'D-Engine-BenchRunner.exe'
    if not exe.exists():
        print(f"ERROR: BenchRunner not found: {exe}", file=sys.stderr)
        return 1

    root = Path('artifacts') / 'bench' / 'sweeps'

    runs: list[tuple[str, Path]] = []
    for s in args.sampling:
        for h in args.shards:
            for b in args.batch:
                combo = f"s{s}-h{h}-b{b}"
                out_dir = root / combo
                code = run_once(exe, out_dir, s, h, b, args.warmup, args.repeat)
                if code != 0:
                    print(f"WARN: Run failed for {combo} with exit code {code}", file=sys.stderr)
                runs.append((combo, out_dir))

    if args.emit_md:
        emit_md_table(Path(args.emit_md), runs)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
