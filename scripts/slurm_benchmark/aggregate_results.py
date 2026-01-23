#!/usr/bin/env python3
"""
Aggregate per-run JSON files produced by the Slurm benchmark into a single
JSON file and print a short summary.
"""
import argparse
import json
from pathlib import Path
import os
import time


def aggregate(results_dir, out_file):
    p = Path(results_dir)
    files = list(p.glob('*.json'))
    data = {}
    for f in files:
        try:
            j = json.loads(f.read_text())
        except Exception:
            continue
        key = os.path.basename(j.get('pattern_file') or f.name)
        data.setdefault(key, []).append(j)

    out = {'generated_at': time.time(), 'patterns': {}}
    for k, runs in data.items():
        out['patterns'][k] = runs

    Path(out_file).write_text(json.dumps(out, indent=2))
    print(f'Wrote aggregated results: {out_file} ({len(files)} run files)')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('results_dir')
    parser.add_argument('out_file')
    args = parser.parse_args()
    aggregate(args.results_dir, args.out_file)


if __name__ == '__main__':
    main()
