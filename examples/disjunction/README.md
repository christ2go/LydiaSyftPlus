# Disjunction Benchmark Family

This directory contains obligation fragment benchmarks featuring disjunctions instead of conjunctions.

## Pattern Types

### Pattern 1: `E(G(∨(ai → F(ei))))`

Formula: `E(G(((!a1 | F(e1))) | ((!a2 | F(e2))) | ... | ((!an | F(en)))))`

**Semantics**: There exists a strategy such that globally, at least one of the implications holds:
- If input `ai` is true, then eventually output `ei` becomes true

This creates a disjunctive safety/guarantee structure where the system must continuously satisfy at least one guarantee.

**Files**: `pattern_1.ltlfplus` through `pattern_30.ltlfplus`

### Pattern 2 (Alternative): `E(F(∨(ai & ei)))`

Formula: `E(F((a1 & e1) | (a2 & e2) | ... | (an & en)))`

**Semantics**: There exists a strategy to eventually reach a state where at least one input-output pair is simultaneously true.

This is a simpler reachability-based disjunctive pattern.

**Files**: `alt_pattern_1.ltlfplus` through `alt_pattern_30.ltlfplus`

## Why Disjunctions?

While the conjunction-based benchmarks (like the "from paper" family) test how solvers handle multiple independent obligations, disjunction-based benchmarks test:

1. **BDD Structure Complexity**: Disjunctions can create different BDD structures than conjunctions
2. **Strategy Generality**: With disjunctions, strategies can be more permissive (only need to satisfy one branch)
3. **Algorithmic Stress-Testing**: Different solving algorithms may handle disjunctive vs conjunctive structures differently

## Comparison with Conjunction Benchmarks

- **From Paper Benchmark**: `AND_{i=1..n} E(F((ai | ei) & X(false)))`
  - Multiple independent existential obligations
  - All must be satisfied
  
- **Disjunction Pattern 1**: `E(G(OR_{i=1..n} (ai → F(ei))))`
  - Single existential quantifier over disjunction
  - Only one branch needs to be satisfied at each step
  
- **Disjunction Pattern 2**: `E(F(OR_{i=1..n} (ai & ei)))`
  - Single reachability goal with multiple choices
  - Even more permissive than Pattern 1

## Generation

To regenerate these benchmarks:

```bash
# Generate Pattern 1 (default)
python3 ../scripts/generate_disjunction_benchmark.py

# Generate Pattern 2 (alternative)
python3 ../scripts/generate_disjunction_benchmark.py --alt
```

## File Format

Each benchmark consists of two files:
- `.ltlfplus`: The LTLf+ formula
- `.part`: Variable partition (inputs/outputs)

Example for `pattern_3`:
```
# pattern_3.ltlfplus
E(G(((!a1 | F(e1))) | ((!a2 | F(e2))) | ((!a3 | F(e3)))))

# pattern_3.part
.inputs: a1 a2 a3
.outputs: e1 e2 e3
```
