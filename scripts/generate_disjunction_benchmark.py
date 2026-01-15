#!/usr/bin/env python3
"""
Generator script for Disjunction-Pattern benchmark family.

This benchmark tests obligation fragment formulas with nested disjunctions.
The formula pattern is:

Ψn = E(G(∨_{1≤i≤n} (ai → F(ei))))

This represents: "There exists a strategy such that globally, 
for each input ai, eventually output ei is satisfied."

The disjunction creates more complex BDD structures compared to conjunctions,
which can stress-test different solving strategies.

For each n from 1 to 30, generates:
- examples/disjunction/pattern_n.ltlfplus
- examples/disjunction/pattern_n.part
"""

import os
import sys

def generate_formula(n):
    """
    Generate the formula: E(G(∨_{1≤i≤n} (ai → F(ei))))
    
    This creates a disjunction of implications, where each implication
    relates an input variable to an eventually satisfied output.
    
    Args:
        n: Number of disjuncts (1 to 30)
    
    Returns:
        (formula_str, input_vars, output_vars)
    """
    disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        ei = f"e{i}"
        # Each disjunct: ai → F(ei)
        # In LTLf syntax: (!ai | F(ei))
        disjunct = f"(!{ai} | F({ei}))"
        disjuncts.append(disjunct)
        input_vars.append(ai)
        output_vars.append(ei)
    
    # Combine with OR and wrap with E(G(...))
    inner_formula = " | ".join(f"({d})" for d in disjuncts)
    formula = f"E(G({inner_formula}))"
    
    return formula, input_vars, output_vars

def generate_alternative_formula(n):
    """
    Alternative pattern: E(F(∨_{1≤i≤n} (ai & ei)))
    
    This represents: "There exists a strategy to eventually reach a state
    where at least one input-output pair is both true."
    
    Args:
        n: Number of disjuncts
    
    Returns:
        (formula_str, input_vars, output_vars)
    """
    disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        ei = f"e{i}"
        # Each disjunct: ai & ei
        disjunct = f"({ai} & {ei})"
        disjuncts.append(disjunct)
        input_vars.append(ai)
        output_vars.append(ei)
    
    # Combine with OR and wrap with E(F(...))
    inner_formula = " | ".join(disjuncts)
    formula = f"E(F({inner_formula}))"
    
    return formula, input_vars, output_vars

def write_formula_file(output_dir, n, formula, pattern_name="pattern"):
    """Write the .ltlfplus file"""
    filename = os.path.join(output_dir, f"{pattern_name}_{n}.ltlfplus")
    with open(filename, 'w') as f:
        f.write(formula)
        f.write('\n')
    print(f"Generated: {filename}")

def write_partition_file(output_dir, n, input_vars, output_vars, pattern_name="pattern"):
    """Write the .part file"""
    filename = os.path.join(output_dir, f"{pattern_name}_{n}.part")
    with open(filename, 'w') as f:
        f.write(f".inputs: {' '.join(input_vars)}\n")
        f.write(f".outputs: {' '.join(output_vars)}\n")
    print(f"Generated: {filename}")

def main():
    if len(sys.argv) > 1 and sys.argv[1] in ['-h', '--help']:
        print(__doc__)
        print("\nUsage: python3 generate_disjunction_benchmark.py [--alt]")
        print("\nOptions:")
        print("  --alt    Use alternative formula pattern E(F(∨(ai & ei)))")
        print("           Default pattern is E(G(∨(ai → F(ei))))")
        sys.exit(0)
    
    # Check if alternative pattern requested
    use_alternative = '--alt' in sys.argv
    
    # Create output directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_dir = os.path.join(project_root, "examples", "disjunction")
    
    os.makedirs(output_dir, exist_ok=True)
    
    pattern_func = generate_alternative_formula if use_alternative else generate_formula
    pattern_type = "E(F(∨(ai & ei)))" if use_alternative else "E(G(∨(ai → F(ei))))"
    pattern_name = "alt_pattern" if use_alternative else "pattern"
    
    print(f"Generating Disjunction-Pattern benchmark family (n=1 to 30)")
    print(f"Formula pattern: {pattern_type}")
    print(f"Output directory: {output_dir}\n")
    
    for n in range(1, 31):
        formula, input_vars, output_vars = pattern_func(n)
        write_formula_file(output_dir, n, formula, pattern_name)
        write_partition_file(output_dir, n, input_vars, output_vars, pattern_name)
        if n <= 3 or n % 5 == 0:  # Print formula for first few and every 5th
            print(f"  Formula: {formula}")
        print()
    
    print(f"Generated 30 formula pairs in {output_dir}")

if __name__ == "__main__":
    main()
