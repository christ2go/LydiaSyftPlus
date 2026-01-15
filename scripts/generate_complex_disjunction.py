#!/usr/bin/env python3
"""
Generator script for Complex Disjunction-Pattern benchmark family.

This benchmark creates more challenging obligation fragment formulas with:
1. Nested temporal operators
2. Chains of dependencies
3. Complex disjunctive structures

Pattern Types:

Pattern 1 (Chained Implications):
Ψn = E(G(∨_{1≤i≤n} (ai → F(bi & F(ci & F(ei))))))
"For each input ai, eventually bi holds, then ci holds, then ei holds"

Pattern 2 (Disjunctive Chains):
Ψn = E(G((∨_{1≤i≤n} F(ai & F(ei))) | (∨_{1≤i≤n} F(bi & F(fi)))))
"Either achieve one of the a-e chains OR one of the b-f chains"

Pattern 3 (Nested Until):
Ψn = E(∨_{1≤i≤n} (G(ai → F(ei)) & G(bi → F(fi))))
"Pick one index i where both implications hold globally"

For each n from 1 to 30, generates formulas with increasing complexity.
"""

import os
import sys

def generate_chained_implications(n):
    """
    Pattern 1: E(G(∨_{1≤i≤n} (ai → F(bi & F(ci & F(ei))))))
    
    Each disjunct has a chain of 3 eventual obligations.
    This creates deeper DFA states and more complex Büchi games.
    """
    disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        bi = f"b{i}"
        ci = f"c{i}"
        ei = f"e{i}"
        
        # Chain: ai → F(bi & F(ci & F(ei)))
        chain = f"(!{ai} | F({bi} & F({ci} & F({ei}))))"
        disjuncts.append(chain)
        
        input_vars.append(ai)
        output_vars.extend([bi, ci, ei])
    
    inner_formula = " | ".join(f"({d})" for d in disjuncts)
    formula = f"E(G({inner_formula}))"
    
    return formula, input_vars, output_vars

def generate_disjunctive_chains(n):
    """
    Pattern 2: E(G((∨_{1≤i≤n} F(ai & F(ei))) | (∨_{1≤i≤n} F(bi & F(fi)))))
    
    Two groups of disjunctive chains - solver must choose which group to satisfy.
    """
    group1_disjuncts = []
    group2_disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        bi = f"b{i}"
        ei = f"e{i}"
        fi = f"f{i}"
        
        # Group 1: F(ai & F(ei))
        group1_disjuncts.append(f"F({ai} & F({ei}))")
        # Group 2: F(bi & F(fi))
        group2_disjuncts.append(f"F({bi} & F({fi}))")
        
        input_vars.extend([ai, bi])
        output_vars.extend([ei, fi])
    
    group1 = " | ".join(f"({d})" for d in group1_disjuncts)
    group2 = " | ".join(f"({d})" for d in group2_disjuncts)
    
    formula = f"E(G(({group1}) | ({group2})))"
    
    return formula, input_vars, output_vars

def generate_nested_until(n):
    """
    Pattern 3: E(∨_{1≤i≤n} (G(ai → F(ei)) & G(bi → F(fi))))
    
    Existential quantifier over disjunction of conjunctive obligations.
    Each branch has two global implications that must both hold.
    """
    disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        bi = f"b{i}"
        ei = f"e{i}"
        fi = f"f{i}"
        
        # Each disjunct: G(ai → F(ei)) & G(bi → F(fi))
        impl1 = f"G(!{ai} | F({ei}))"
        impl2 = f"G(!{bi} | F({fi}))"
        disjunct = f"({impl1} & {impl2})"
        disjuncts.append(disjunct)
        
        input_vars.extend([ai, bi])
        output_vars.extend([ei, fi])
    
    inner_formula = " | ".join(f"({d})" for d in disjuncts)
    formula = f"E({inner_formula})"
    
    return formula, input_vars, output_vars

def generate_interleaved_dependencies(n):
    """
    Pattern 4: E(G(∨_{1≤i≤n} ((ai & X(F(ei))) | (bi & F(X(fi))))))
    
    Mix of X and F operators to create complex temporal dependencies.
    """
    disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        bi = f"b{i}"
        ei = f"e{i}"
        fi = f"f{i}"
        
        # Each disjunct has two alternatives with interleaved X and F
        alt1 = f"({ai} & X(F({ei})))"
        alt2 = f"({bi} & F(X({fi})))"
        disjunct = f"({alt1} | {alt2})"
        disjuncts.append(disjunct)
        
        input_vars.extend([ai, bi])
        output_vars.extend([ei, fi])
    
    inner_formula = " | ".join(f"({d})" for d in disjuncts)
    formula = f"E(G({inner_formula}))"
    
    return formula, input_vars, output_vars

def generate_layered_reachability(n):
    """
    Pattern 5: E(∨_{1≤i≤n} F(ai & ∨_{1≤j≤n} F(ej & F(fj))))
    
    Two-layer disjunctive reachability - very permissive but complex.
    """
    outer_disjuncts = []
    input_vars = []
    output_vars = []
    
    for i in range(1, n + 1):
        ai = f"a{i}"
        input_vars.append(ai)
        
        # Inner disjunction over all outputs
        inner_disjuncts = []
        for j in range(1, n + 1):
            ej = f"e{j}"
            fj = f"f{j}"
            if i == 1:  # Only add to output_vars once
                output_vars.extend([ej, fj])
            inner_disjuncts.append(f"F({ej} & F({fj}))")
        
        inner = " | ".join(f"({d})" for d in inner_disjuncts)
        outer_disjuncts.append(f"F({ai} & ({inner}))")
    
    outer = " | ".join(f"({d})" for d in outer_disjuncts)
    formula = f"E({outer})"
    
    return formula, input_vars, output_vars

PATTERNS = {
    'chained': ('chained', generate_chained_implications, 
                "Chained implications: E(G(∨(ai → F(bi & F(ci & F(ei))))))"),
    'dual': ('dual', generate_disjunctive_chains,
             "Dual chains: E(G((∨F(ai & F(ei))) | (∨F(bi & F(fi)))))"),
    'nested': ('nested', generate_nested_until,
               "Nested until: E(∨(G(ai → F(ei)) & G(bi → F(fi))))"),
    'interleaved': ('interleaved', generate_interleaved_dependencies,
                    "Interleaved X/F: E(G(∨((ai & X(F(ei))) | (bi & F(X(fi))))))"),
    'layered': ('layered', generate_layered_reachability,
                "Layered reachability: E(∨F(ai & ∨F(ej & F(fj))))"),
}

def write_formula_file(output_dir, n, formula, pattern_name):
    """Write the .ltlfplus file"""
    filename = os.path.join(output_dir, f"{pattern_name}_{n}.ltlfplus")
    with open(filename, 'w') as f:
        f.write(formula)
        f.write('\n')
    return filename

def write_partition_file(output_dir, n, input_vars, output_vars, pattern_name):
    """Write the .part file"""
    filename = os.path.join(output_dir, f"{pattern_name}_{n}.part")
    with open(filename, 'w') as f:
        f.write(f".inputs: {' '.join(input_vars)}\n")
        f.write(f".outputs: {' '.join(output_vars)}\n")
    return filename

def main():
    if len(sys.argv) > 1 and sys.argv[1] in ['-h', '--help']:
        print(__doc__)
        print("\nAvailable patterns:")
        for key, (name, func, desc) in PATTERNS.items():
            print(f"  --{key:12s} : {desc}")
        print("\nUsage: python3 generate_complex_disjunction.py [--pattern]")
        print("       If no pattern specified, generates all patterns.")
        sys.exit(0)
    
    # Determine which patterns to generate
    patterns_to_gen = []
    for arg in sys.argv[1:]:
        if arg.startswith('--'):
            key = arg[2:]
            if key in PATTERNS:
                patterns_to_gen.append(key)
    
    # If no patterns specified, generate all
    if not patterns_to_gen:
        patterns_to_gen = list(PATTERNS.keys())
    
    # Create output directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_dir = os.path.join(project_root, "examples", "complex_disj")
    
    os.makedirs(output_dir, exist_ok=True)
    
    print(f"Generating Complex Disjunction benchmarks (n=1 to 30)")
    print(f"Output directory: {output_dir}\n")
    
    total_generated = 0
    for pattern_key in patterns_to_gen:
        pattern_name, pattern_func, pattern_desc = PATTERNS[pattern_key]
        print(f"\n{'='*70}")
        print(f"Pattern: {pattern_name}")
        print(f"Description: {pattern_desc}")
        print(f"{'='*70}\n")
        
        for n in range(1, 31):
            formula, input_vars, output_vars = pattern_func(n)
            f_file = write_formula_file(output_dir, n, formula, pattern_name)
            p_file = write_partition_file(output_dir, n, input_vars, output_vars, pattern_name)
            
            if n <= 2 or n % 10 == 0:  # Print formula for first two and every 10th
                print(f"n={n:2d}: Generated {os.path.basename(f_file)}")
                if n <= 2:
                    print(f"      Formula: {formula[:100]}{'...' if len(formula) > 100 else ''}")
                print(f"      Inputs: {len(input_vars)}, Outputs: {len(output_vars)}")
            
            total_generated += 2
    
    print(f"\n{'='*70}")
    print(f"Generated {total_generated} files ({total_generated//2} formula pairs) in {output_dir}")

if __name__ == "__main__":
    main()
