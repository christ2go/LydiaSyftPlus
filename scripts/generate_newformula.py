#!/usr/bin/env python3
"""
Generator script for ∃-Pattern benchmark family from paper.

The formula pattern is:
Ψn = AND_{1≤i≤n} ∃F((ai ∨ei) ∧X(false))

For each n from 1 to 20, generates:
- examples/frompaper/pattern_n.ltlfplus
- examples/frompaper/pattern_n.part
"""

import os
import sys

def generate_formula(n):
    """
    Generate the formula: AND_{1≤i≤n} \forall F((ai)) -> E(F(e_i))
    
    Args:
        n: Number of conjuncts (1 to 10)
    
    Returns:
        (formula_str, input_vars, output_vars)
    """
    conjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        ei = f"e{i}"
        input_vars.append(ai)
        output_vars.append(ei)
        #    Generate the formula: A(F(ai)) -> E(F(ei))
        # In LTLf+ syntax: (A(F(ai)) -> E(F(ei)))
        conj = f"(E(F({ai})) -> E(F({ei})))"
        conjuncts.append(conj)

    # Combine with AND
    formula = " & ".join(f"({c})" for c in conjuncts)
    
    return formula, input_vars, output_vars

def write_formula_file(output_dir, n, formula):
    """Write the .ltlfplus file"""
    filename = os.path.join(output_dir, f"pattern_{n}.ltlfplus")
    with open(filename, 'w') as f:
        f.write(formula)
        f.write('\n')
    print(f"Generated: {filename}")

def write_partition_file(output_dir, n, input_vars, output_vars):
    """Write the .part file"""
    filename = os.path.join(output_dir, f"pattern_{n}.part")
    with open(filename, 'w') as f:
        f.write(f".inputs: {' '.join(input_vars)}\n")
        f.write(f".outputs: {' '.join(output_vars)}\n")
    print(f"Generated: {filename}")

def main():
    # Create output directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_dir = os.path.join(project_root, "examples", "obligations_guarantees")
    
    os.makedirs(output_dir, exist_ok=True)
    
    print(f"Generating obligation guarantee Pattern benchmark family (n=1 to 10)")
    print(f"Output directory: {output_dir}\n")
    
    for n in range(1, 30):
        formula, input_vars, output_vars = generate_formula(n)
        write_formula_file(output_dir, n, formula)
        write_partition_file(output_dir, n, input_vars, output_vars)
        print()
    
    print(f"Generated {20} formula pairs in {output_dir}")

if __name__ == "__main__":
    main()





