#!/usr/bin/env python3
"""
Parse BDD dump from WeakGameSolver and generate explicit DFA with Graphviz visualization.

Usage:
    ./bin/LydiaSyftEL ... 2>&1 | python scripts/visualize_dfa.py > dfa.dot
    dot -Tpng dfa.dot -o dfa.png
    
    # Or with JSON input:
    python scripts/visualize_dfa.py dfa.json -o dfa.png
"""

import sys
import json
from collections import defaultdict

try:
    from sympy import symbols, SOPform
    HAS_SYMPY = True
except ImportError:
    HAS_SYMPY = False
    print("Warning: sympy not available, transition labels won't be minimized", file=sys.stderr)



def parse_json_dfa(json_data):
    """Parse DFA from JSON format."""
    if isinstance(json_data, str):
        data = json.loads(json_data)
    else:
        data = json_data
    
    dfa = {
        'num_state_bits': data['num_state_bits'],
        'num_inputs': data['num_inputs'],
        'num_outputs': data['num_outputs'],
        'state_var_indices': data.get('state_var_indices', []),
        'input_labels': data.get('input_labels', []),
        'output_labels': data.get('output_labels', []),
        'trans_funcs': {},
        'accepting_minterms': data.get('accepting_minterms', []),
        'initial_minterm': data.get('initial_minterm', '')
    }
    
    # Parse transition functions
    for bit_str, minterms in data.get('trans_funcs', {}).items():
        bit = int(bit_str)
        dfa['trans_funcs'][bit] = [tuple(m) for m in minterms]
    
    return dfa


def parse_dfa_dump(lines):
    """Parse the PYDFA dump from solver output."""
    dfa = {
        'num_state_bits': 0,
        'num_inputs': 0,
        'num_outputs': 0,
        'state_var_indices': [],
        'input_labels': [],
        'output_labels': [],
        'trans_funcs': {},  # bit -> list of (state, input, output) tuples
        'accepting_minterms': [],
        'initial_minterm': ''
    }
    
    in_dump = False
    
    for line in lines:
        line = line.strip()
        
        if line == '===PYDFA_BEGIN===':
            in_dump = True
            continue
        elif line == '===PYDFA_END===':
            in_dump = False
            continue
        
        if not in_dump:
            continue
        
        if '=' not in line:
            continue
            
        key, val = line.split('=', 1)
        
        if key == 'num_state_bits':
            dfa['num_state_bits'] = int(val)
        elif key == 'num_inputs':
            dfa['num_inputs'] = int(val)
        elif key == 'num_outputs':
            dfa['num_outputs'] = int(val)
        elif key == 'state_var_indices':
            if val:
                dfa['state_var_indices'] = [int(x) for x in val.split(',')]
        elif key == 'input_labels':
            if val:
                dfa['input_labels'] = val.split(',')
        elif key == 'output_labels':
            if val:
                dfa['output_labels'] = val.split(',')
        elif key.startswith('trans_func_'):
            bit = int(key.split('_')[2])
            if val:
                # Parse format: state,input,output;state,input,output;...
                minterms = []
                for entry in val.split(';'):
                    parts = entry.split(',')
                    if len(parts) == 3:
                        minterms.append((int(parts[0]), int(parts[1]), int(parts[2])))
                dfa['trans_funcs'][bit] = minterms
            else:
                dfa['trans_funcs'][bit] = []
        elif key == 'accepting_minterms':
            if val:
                dfa['accepting_minterms'] = val.split(';')
        elif key == 'initial_minterm':
            dfa['initial_minterm'] = val
    
    return dfa


def minterm_to_int(minterm):
    """Convert binary string to integer (LSB first)."""
    result = 0
    for i, c in enumerate(minterm):
        if c == '1':
            result |= (1 << i)
    return result


def build_explicit_dfa(dfa):
    """Build explicit DFA from BDD minterms."""
    num_state_bits = dfa['num_state_bits']
    num_inputs = dfa['num_inputs']
    num_outputs = dfa['num_outputs']
    
    num_states = 1 << num_state_bits
    num_io_combos = 1 << (num_inputs + num_outputs)
    
    # Parse initial state
    initial_state = minterm_to_int(dfa['initial_minterm']) if dfa['initial_minterm'] else 0
    
    # Parse accepting states
    accepting_states = set()
    for m in dfa['accepting_minterms']:
        if m:
            accepting_states.add(minterm_to_int(m))
    
    # Build lookup: for each bit, which (state, input, output) gives 1
    trans_lookup = {}
    for bit, minterms in dfa['trans_funcs'].items():
        trans_lookup[bit] = set(minterms)
    
    # Build explicit transitions: (state, input, output) -> next_state
    # Also group by (state, next_state) -> list of (input, output) values
    transitions = {}  # (state, input, output) -> next_state
    trans_by_edge = defaultdict(list)  # (state, next_state) -> [(input, output)]
    
    for state in range(num_states):
        for inp in range(1 << num_inputs):
            for out in range(1 << num_outputs):
                # Compute next state by checking each bit
                next_state = 0
                for bit in range(num_state_bits):
                    if bit in trans_lookup and (state, inp, out) in trans_lookup[bit]:
                        next_state |= (1 << bit)
                
                transitions[(state, inp, out)] = next_state
                trans_by_edge[(state, next_state)].append((inp, out))
    
    return {
        'num_states': num_states,
        'num_state_bits': num_state_bits,
        'num_inputs': num_inputs,
        'num_outputs': num_outputs,
        'initial_state': initial_state,
        'accepting_states': accepting_states,
        'transitions': transitions,
        'trans_by_edge': trans_by_edge,
        'input_labels': dfa['input_labels'],
        'output_labels': dfa['output_labels']
    }


def minimize_label(io_pairs, num_inputs, num_outputs, input_labels, output_labels):
    """Minimize transition label using sympy."""
    num_io = num_inputs + num_outputs
    all_io = 1 << num_io
    
    # If all I/O values lead to same transition, return "true"
    if len(io_pairs) == all_io:
        return 'true'
    
    # Build label names
    in_labels = list(input_labels) if input_labels else [f'i{i}' for i in range(num_inputs)]
    out_labels = list(output_labels) if output_labels else [f'o{i}' for i in range(num_outputs)]
    all_labels = in_labels + out_labels
    
    if not HAS_SYMPY:
        # Just return first pair as example
        if io_pairs:
            inp, out = io_pairs[0]
            parts = []
            for i in range(num_inputs):
                label = in_labels[i] if i < len(in_labels) else f'i{i}'
                if (inp >> i) & 1:
                    parts.append(label)
                else:
                    parts.append(f'!{label}')
            for i in range(num_outputs):
                label = out_labels[i] if i < len(out_labels) else f'o{i}'
                if (out >> i) & 1:
                    parts.append(label)
                else:
                    parts.append(f'!{label}')
            if len(io_pairs) > 1:
                return f"({' & '.join(parts)}) | ..."
            return ' & '.join(parts)
        return 'true'
    
    # Create symbols
    syms = symbols(all_labels[:num_io])
    
    # Build minterms list for SOPform
    minterms = []
    for inp, out in io_pairs:
        io_val = inp | (out << num_inputs)
        minterm = tuple((io_val >> i) & 1 for i in range(num_io))
        minterms.append(minterm)
    
    try:
        expr = SOPform(syms, minterms)
        result = str(expr)
        # Clean up sympy output
        result = result.replace('~', '!')
        result = result.replace(' & ', ' ∧ ')
        result = result.replace(' | ', ' ∨ ')
        return result
    except:
        return f'{len(io_pairs)} conditions'


def compute_sccs(explicit_dfa, reachable):
    """Compute SCCs using Tarjan's algorithm."""
    graph = defaultdict(set)
    for (src, dst), _ in explicit_dfa['trans_by_edge'].items():
        if src in reachable and dst in reachable:
            graph[src].add(dst)
    
    index_counter = [0]
    stack = []
    lowlinks = {}
    index = {}
    on_stack = {}
    sccs = []
    
    def strongconnect(v):
        index[v] = index_counter[0]
        lowlinks[v] = index_counter[0]
        index_counter[0] += 1
        stack.append(v)
        on_stack[v] = True
        
        for w in graph.get(v, []):
            if w not in index:
                strongconnect(w)
                lowlinks[v] = min(lowlinks[v], lowlinks[w])
            elif on_stack.get(w, False):
                lowlinks[v] = min(lowlinks[v], index[w])
        
        if lowlinks[v] == index[v]:
            scc = []
            while True:
                w = stack.pop()
                on_stack[w] = False
                scc.append(w)
                if w == v:
                    break
            sccs.append(set(scc))
    
    for v in reachable:
        if v not in index:
            strongconnect(v)
    
    return sccs


def check_weak_dfa(explicit_dfa, sccs):
    """
    Check if DFA is weak: each SCC must be entirely accepting or entirely rejecting.
    Returns (is_weak, problematic_sccs) where problematic_sccs lists mixed SCCs.
    """
    accepting = explicit_dfa['accepting_states']
    problematic = []
    
    for scc in sccs:
        if len(scc) <= 1:
            continue  # Trivial SCCs are always weak
        
        accepting_in_scc = scc & accepting
        rejecting_in_scc = scc - accepting
        
        if accepting_in_scc and rejecting_in_scc:
            problematic.append({
                'scc': scc,
                'accepting': accepting_in_scc,
                'rejecting': rejecting_in_scc
            })
    
    return len(problematic) == 0, problematic


def generate_graphviz(explicit_dfa, all_states=False):
    """Generate Graphviz DOT representation."""
    lines = []
    lines.append('digraph DFA {')
    lines.append('    rankdir=LR;')
    lines.append('    node [shape=circle, fontname="monospace"];')
    lines.append('    edge [fontname="monospace", fontsize=10];')
    lines.append('')
    
    # Initial state marker
    lines.append('    __start [shape=none, label=""];')
    lines.append(f'    __start -> {explicit_dfa["initial_state"]};')
    lines.append('')
    
    # Determine reachable states
    if all_states:
        reachable = set(range(explicit_dfa['num_states']))
    else:
        reachable = set()
        reachable.add(explicit_dfa['initial_state'])
        changed = True
        while changed:
            changed = False
            for (src, dst), _ in explicit_dfa['trans_by_edge'].items():
                if src in reachable and dst not in reachable:
                    reachable.add(dst)
                    changed = True
    
    # Compute SCCs and assign colors
    sccs = compute_sccs(explicit_dfa, reachable)
    
    # Check if DFA is weak
    is_weak, problematic_sccs = check_weak_dfa(explicit_dfa, sccs)
    if not is_weak:
        print("=" * 60, file=sys.stderr)
        print("WARNING: DFA IS NOT WEAK!", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        for p in problematic_sccs:
            print(f"  Mixed SCC: {sorted(p['scc'])}", file=sys.stderr)
            print(f"    Accepting states: {sorted(p['accepting'])}", file=sys.stderr)
            print(f"    Rejecting states: {sorted(p['rejecting'])}", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
    
    scc_colors = [
        '#FFB3BA', '#BAFFC9', '#BAE1FF', '#FFFFBA', '#FFDFba',
        '#E0BBE4', '#957DAD', '#D4A5A5', '#A8E6CF', '#DCEDC1'
    ]
    state_to_scc_color = {}
    for i, scc in enumerate(sccs):
        color = scc_colors[i % len(scc_colors)]
        for s in scc:
            state_to_scc_color[s] = color
    
    # Node definitions
    for s in range(explicit_dfa['num_states']):
        if s not in reachable:
            continue
        
        shape = 'doublecircle' if s in explicit_dfa['accepting_states'] else 'circle'
        border_color = 'green' if s in explicit_dfa['accepting_states'] else 'black'
        fill_color = state_to_scc_color.get(s, 'white')
        lines.append(f'    {s} [shape={shape}, color={border_color}, style=filled, fillcolor="{fill_color}", label="{s}"];')
    
    lines.append('')
    
    # Transitions with minimized labels
    for (src, dst), io_pairs in explicit_dfa['trans_by_edge'].items():
        if src not in reachable:
            continue
        
        label = minimize_label(
            io_pairs,
            explicit_dfa['num_inputs'],
            explicit_dfa['num_outputs'],
            explicit_dfa['input_labels'],
            explicit_dfa['output_labels']
        )
        
        # Escape for DOT
        label = label.replace('"', '\\"')
        lines.append(f'    {src} -> {dst} [label="{label}"];')
    
    lines.append('}')
    
    return '\n'.join(lines)


def main():
    import argparse
    import subprocess
    import tempfile
    import os
    
    parser = argparse.ArgumentParser(description='Visualize DFA from solver output')
    parser.add_argument('input', nargs='?', help='Input file (.json or text with PYDFA dump, default: stdin)')
    parser.add_argument('-o', '--output', help='Output PNG file (default: dfa.png)')
    parser.add_argument('--dot-only', action='store_true', help='Only output DOT to stdout, no PNG')
    parser.add_argument('--all-states', action='store_true', help='Show all states, not just reachable ones')
    parser.add_argument('--json', action='store_true', help='Force JSON parsing (auto-detected by .json extension)')
    args = parser.parse_args()
    
    # Determine input format
    is_json = args.json or (args.input and args.input.endswith('.json'))
    
    # Read input
    if args.input:
        with open(args.input, 'r') as f:
            if is_json:
                content = f.read()
                dfa = parse_json_dfa(content)
            else:
                lines = f.readlines()
                dfa = parse_dfa_dump(lines)
    else:
        lines = sys.stdin.readlines()
        # Try JSON first, fall back to text format
        if is_json:
            content = ''.join(lines)
            try:
                dfa = parse_json_dfa(content)
            except json.JSONDecodeError:
                print("Error: Invalid JSON input", file=sys.stderr)
                return 1
        else:
            dfa = parse_dfa_dump(lines)
    
    if dfa['num_state_bits'] == 0:
        print("Error: No DFA dump found in input", file=sys.stderr)
        print("Make sure the input contains ===PYDFA_BEGIN=== ... ===PYDFA_END=== or valid JSON", file=sys.stderr)
        return 1
    
    # Build explicit DFA
    explicit_dfa = build_explicit_dfa(dfa)
    
    # Print some info to stderr
    print(f"Parsed DFA:", file=sys.stderr)
    print(f"  State bits: {explicit_dfa['num_state_bits']}", file=sys.stderr)
    print(f"  States: {explicit_dfa['num_states']}", file=sys.stderr)
    print(f"  Inputs: {explicit_dfa['input_labels']}", file=sys.stderr)
    print(f"  Outputs: {explicit_dfa['output_labels']}", file=sys.stderr)
    print(f"  Initial state: {explicit_dfa['initial_state']}", file=sys.stderr)
    print(f"  Accepting states: {explicit_dfa['accepting_states']}", file=sys.stderr)
    print(f"  Edges: {len(explicit_dfa['trans_by_edge'])}", file=sys.stderr)
    
    # Generate DOT
    dot = generate_graphviz(explicit_dfa, all_states=args.all_states)
    
    if args.dot_only:
        print(dot)
        return 0
    
    # Generate PNG
    output_file = args.output or 'dfa.png'
    
    try:
        
        result = subprocess.run(
            ['dot', '-Tpng', '-o', output_file],
            input=dot,
            text=True,
            capture_output=True
        )
        if result.returncode != 0:
            print(f"Error running dot: {result.stderr}", file=sys.stderr)
            return 1
        print(f"Generated: {output_file}", file=sys.stderr)
    except FileNotFoundError:
        print("Error: 'dot' command not found. Install graphviz.", file=sys.stderr)
        print("Falling back to DOT output:", file=sys.stderr)
        print(dot)
        return 1
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
