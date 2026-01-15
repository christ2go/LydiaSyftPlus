#!/usr/bin/env python3
"""
Parse WeakGameSolver debug output and build a simple visualization/summary
of states and how they are classified across layers.

Usage:
    python scripts/visualize_solver_trace.py --dot out.dot trace.txt
    cat trace.txt | python scripts/visualize_solver_trace.py --dot out.dot

Outputs:
    - Prints a per-layer summary of sets: layer, avoid_bad, accepting_winners,
      reach_good, layer_good, layer_bad, good_states_after, bad_states_after.
    - If --dot is provided, writes a Graphviz DOT with transitions and
      final good/bad coloring (good=green, bad=red, unknown=gray).
"""

import argparse
import sys
import re
from collections import defaultdict


SET_PAT = re.compile(r"\[DEBUG\]\s+([A-Za-z_ ]+)\s*\(\d+ states\)\s*=\s*\{([^}]*)\}")
TRANS_PAT = re.compile(r"\s*(\d+)\s*->\s*(\d+)")
LAYER_PAT = re.compile(r"Processing layer\s+(\d+)")
GOOD_AFTER_PAT = re.compile(r"\[DEBUG\]\s+good_states \(after\)\s*\(\d+ states\)\s*=\s*\{([^}]*)\}")
BAD_AFTER_PAT = re.compile(r"\[DEBUG\]\s+bad_states \(after\)\s*\(\d+ states\)\s*\{([^}]*)\}")


def parse_states_list(txt: str):
    txt = txt.strip()
    if not txt:
        return set()
    return {int(x) for x in txt.split(",") if x.strip()}


def parse_trace(lines):
    transitions = set()
    layers = defaultdict(lambda: defaultdict(set))
    layer_inputs = defaultdict(str)
    current_layer = None
    good_after = set()
    bad_after = set()

    for line in lines:
        m = LAYER_PAT.search(line)
        if m:
            current_layer = int(m.group(1))
        if current_layer is not None and "input_labels" in line:
            # capture input/output labels if present
            if "input_labels" in line:
                layer_inputs[current_layer] = line.strip()
        m = SET_PAT.search(line)
        if m and current_layer is not None:
            name = m.group(1).strip()
            states = parse_states_list(m.group(2))
            layers[current_layer][name] = states
        m = TRANS_PAT.search(line)
        if m:
            transitions.add((int(m.group(1)), int(m.group(2))))
        m = GOOD_AFTER_PAT.search(line)
        if m:
            good_after = parse_states_list(m.group(1))
        m = BAD_AFTER_PAT.search(line)
        if m:
            bad_after = parse_states_list(m.group(1))
    return layers, transitions, good_after, bad_after


def emit_dot(transitions, good_after, bad_after, outfile):
    with open(outfile, "w", encoding="utf-8") as f:
        f.write("digraph G {\n")
        f.write('  rankdir=LR;\n  node [shape=circle];\n')
        for u, v in sorted(transitions):
            f.write(f"  {u} -> {v};\n")
        for s in sorted(good_after):
            f.write(f'  {s} [style=filled, fillcolor="palegreen"];\n')
        for s in sorted(bad_after):
            if s in good_after:
                continue
            f.write(f'  {s} [style=filled, fillcolor="lightcoral"];\n')
        f.write("}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", nargs="?", help="trace file (default: stdin)")
    ap.add_argument("--dot", help="write DOT graph to file")
    args = ap.parse_args()

    if args.trace:
        with open(args.trace, "r", encoding="utf-8") as fh:
            lines = fh.readlines()
    else:
        lines = sys.stdin.readlines()

    layers, transitions, good_after, bad_after = parse_trace(lines)

    print("=== Layer summaries ===")
    for idx in sorted(layers):
        print(f"Layer {idx}:")
        # compute remained and dropped states if both layer and CPre exist_output/result are present
        layer_set = layers[idx].get("layer", set())
        ex = layers[idx].get("CPreSystem exists_output states", set())
        res = layers[idx].get("CPreSystem result", set())
        if ex:
            dropped = sorted(ex - res)
            if dropped:
                print(f"  dropped by forall_input: {dropped}")
        for key, states in layers[idx].items():
            print(f"  {key}: {sorted(states)}")
    print("\nFinal good:", sorted(good_after))
    print("Final bad:", sorted(bad_after))

    if args.dot:
        emit_dot(transitions, good_after, bad_after, args.dot)
        print(f"\nDOT written to {args.dot}")


if __name__ == "__main__":
    main()

