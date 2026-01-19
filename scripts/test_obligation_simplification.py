#!/usr/bin/env python3
"""
Test script that generates random LTLf+ formulas in the obligation fragment
and compares results between normal solver and obligation simplification.
"""

import subprocess
import random
import sys
import tempfile
import os
import argparse
import time
import curses
from pathlib import Path

# Atomic propositions to use
ATOMS = ['p', 'q', 'r', 's', 't']

def random_ltlf_formula(depth=3, atoms=None):
    """Generate a random LTLf formula."""
    if atoms is None:
        atoms = ATOMS[:3]  # Use first 3 atoms by default
    
    if depth <= 0 or random.random() < 0.3:
        # Base case: atomic proposition or its negation
        atom = random.choice(atoms)
        if random.random() < 0.3:
            return f"!{atom}"
        return atom
    
    # Recursive case: choose an operator
    op = random.choice(['and', 'or', 'X', 'F', 'G', 'U', 'impl'])
    
    if op == 'and':
        left = random_ltlf_formula(depth - 1, atoms)
        right = random_ltlf_formula(depth - 1, atoms)
        return f"({left} & {right})"
    elif op == 'or':
        left = random_ltlf_formula(depth - 1, atoms)
        right = random_ltlf_formula(depth - 1, atoms)
        return f"({left} | {right})"
    elif op == 'X':
        sub = random_ltlf_formula(depth - 1, atoms)
        return f"X({sub})"
    elif op == 'F':
        sub = random_ltlf_formula(depth - 1, atoms)
        return f"F({sub})"
    elif op == 'G':
        sub = random_ltlf_formula(depth - 1, atoms)
        return f"G({sub})"
    elif op == 'U':
        left = random_ltlf_formula(depth - 1, atoms)
        right = random_ltlf_formula(depth - 1, atoms)
        return f"({left} U {right})"
    elif op == 'impl':
        left = random_ltlf_formula(depth - 1, atoms)
        right = random_ltlf_formula(depth - 1, atoms)
        return f"({left} -> {right})"
    
    return random.choice(atoms)

def random_obligation_formula(num_conjuncts=2, depth=2, atoms=None):
    """
    Generate a random LTLf+ formula in the obligation fragment.
    Obligation fragment consists of positive boolean combinations of:
    - A(phi) - Forall quantifier (safety-like)
    - E(phi) - Exists quantifier (guarantee-like)
    where phi is an LTLf formula.
    """
    if atoms is None:
        atoms = ATOMS[:3]
    
    def random_obligation_subformula(d):
        if d <= 0 or random.random() < 0.4:
            # Base case: A(phi) or E(phi)
            inner = random_ltlf_formula(depth, atoms)
            if random.random() < 0.5:
                return f"A({inner})"
            else:
                return f"E({inner})"
        
        # Recursive case: conjunction or disjunction (positive boolean combination)
        op = random.choice(['and', 'or'])
        left = random_obligation_subformula(d - 1)
        right = random_obligation_subformula(d - 1)
        
        if op == 'and':
            return f"({left} & {right})"
        else:
            return f"({left} | {right})"
    
    return random_obligation_subformula(num_conjuncts)

def random_partition(atoms):
    """Generate a random partition of atoms into inputs and outputs."""
    atoms = list(atoms)
    random.shuffle(atoms)
    
    # Ensure at least one input and one output
    if len(atoms) < 2:
        return atoms, []
    
    split = random.randint(1, len(atoms) - 1)
    inputs = atoms[:split]
    outputs = atoms[split:]
    
    return inputs, outputs

def write_formula_file(formula, filepath):
    """Write formula to a .ltlfplus file."""
    with open(filepath, 'w') as f:
        f.write(formula + "\n")

def write_partition_file(inputs, outputs, filepath):
    """Write partition to a .part file."""
    with open(filepath, 'w') as f:
        f.write(".inputs: " + " ".join(inputs) + "\n")
        f.write(".outputs: " + " ".join(outputs) + "\n")

def run_solver(binary_path, formula_file, partition_file, obligation_simplification, solver_id="0", timeout=60):
    """Run the solver and return (status, realizable, output).

    status ∈ {"ok", "timeout", "error"}.
    """
    cmd = [
        binary_path,
        "-i", formula_file,
        "-p", partition_file,
        "-s", "0",  # starting player
        "-g", solver_id,  # solver
        "--obligation-simplification", str(obligation_simplification)
    ]
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        
        output = result.stdout + result.stderr
        
        # Parse realizability from output
        realizable = None
        if "REALIZABLE" in output.upper():
            if "UNREALIZABLE" in output.upper():
                realizable = False
            else:
                realizable = True
        
        return "ok", realizable, output
    
    except subprocess.TimeoutExpired:
        return "timeout", None, "TIMEOUT"
    except Exception as e:
        return "error", None, str(e)

def test_single_formula(binary_path, formula, inputs, outputs, verbose=False, timeout=60):
    """Test a single formula with the obligation simplifier and baseline solvers g=0,1,2.
    A mismatch is recorded only if all three baseline solvers disagree with the obligation result.
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        formula_file = os.path.join(tmpdir, "test.ltlfplus")
        partition_file = os.path.join(tmpdir, "test.part")
        
        write_formula_file(formula, formula_file)
        write_partition_file(inputs, outputs, partition_file)
        
        # Run obligation simplification solver (game-solver always 1)
        status_oblig, realizable_oblig, output_oblig = run_solver(
            binary_path, formula_file, partition_file, 1, solver_id="1", timeout=timeout
        )

        # Run baseline solvers g=0,1,2 without obligation simplification
        baseline_results = []
        for gid in ["0", "1", "2"]:
            status, realizable, output = run_solver(
                binary_path, formula_file, partition_file, 0, solver_id=gid, timeout=timeout
            )
            baseline_results.append((gid, status, realizable, output))
        
        if verbose:
            print(f"  Formula: {formula}")
            print(f"  Inputs: {inputs}, Outputs: {outputs}")
            print(f"  Obligation solver (g=1): status={status_oblig}, realizable={realizable_oblig}")
            for gid, st, rea, _ in baseline_results:
                print(f"  Baseline g={gid}: status={st}, realizable={rea}")

        # Classify errors and timeouts
        any_error = (status_oblig == "error") or any(st == "error" for _, st, _, _ in baseline_results)
        
        # Track which solvers timed out
        timeout_solvers = []
        if status_oblig == "timeout":
            timeout_solvers.append("obligation(g=1)")
        for gid, st, _, _ in baseline_results:
            if st == "timeout":
                timeout_solvers.append(f"baseline(g={gid})")
        any_timeout = len(timeout_solvers) > 0

        if any_error:
            return {
                'formula': formula,
                'inputs': inputs,
                'outputs': outputs,
                'status_oblig': status_oblig,
                'realizable_oblig': realizable_oblig,
                'baseline': baseline_results,
                'output_oblig': output_oblig if status_oblig != "ok" else None,
                'agree': None,
                'error': True,
                'timeout': False
            }

        if any_timeout:
            return {
                'formula': formula,
                'inputs': inputs,
                'outputs': outputs,
                'status_oblig': status_oblig,
                'realizable_oblig': realizable_oblig,
                'baseline': baseline_results,
                'output_oblig': output_oblig if status_oblig != "ok" else None,
                'agree': None,
                'error': False,
                'timeout': True,
                'timeout_solvers': timeout_solvers
            }

        # A disagreement is recorded only if all three baselines disagree with obligation result
        baseline_realizabilities = [rea for _, _, rea, _ in baseline_results]
        all_baselines_disagree = all(rea != realizable_oblig for rea in baseline_realizabilities)
        agree = not all_baselines_disagree

        return {
            'formula': formula,
            'inputs': inputs,
            'outputs': outputs,
            'status_oblig': status_oblig,
            'realizable_oblig': realizable_oblig,
            'output_oblig': output_oblig,
            'baseline': baseline_results,
            'agree': agree,
            'error': False,
            'timeout': False
        }

def main():
    parser = argparse.ArgumentParser(
        description='Test obligation simplification against normal solver'
    )
    parser.add_argument(
        '--binary', '-b',
        default='./../build/bin/LydiaSyftEL',
        help='Path to LydiaSyftEL binary'
    )
    parser.add_argument(
        '--num-tests', '-n',
        type=int,
        default=100,
        help='Number of random tests to run'
    )
    parser.add_argument(
        '--num-conjuncts', '-c',
        type=int,
        default=2,
        help='Number of conjuncts in obligation formula'
    )
    parser.add_argument(
        '--depth', '-d',
        type=int,
        default=2,
        help='Depth of inner LTLf formulas'
    )
    parser.add_argument(
        '--num-atoms', '-a',
        type=int,
        default=3,
        help='Number of atomic propositions to use'
    )
    parser.add_argument(
        '--seed', '-s',
        type=int,
        default=None,
        help='Random seed for reproducibility'
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Verbose output'
    )
    parser.add_argument(
        '--shallow',
        action='store_true',
        help='Shallow mode: only print final disagreement count'
    )
    parser.add_argument(
        '--show-smallest',
        action='store_true',
        help='Show 10 smallest disagreements (by formula length)'
    )
    parser.add_argument(
        '--benchmark-runtime',
        type=int,
        default=0,
        help='If >0, run a small runtime benchmark with harder formulas (count)'
    )
    parser.add_argument(
        '--live',
        action='store_true',
        help='Show live failure percentage/progress using curses'
    )
    parser.add_argument(
        '--timeout',
        type=int,
        default=60,
        help='Timeout in seconds for each solver run (default: 60)'
    )
    parser.add_argument(
        '--check-mismatches',
        dest='check_mismatches',
        action='store_true',
        help='Check existing mismatches before running random tests (default: enabled)'
    )
    parser.add_argument(
        '--no-check-mismatches',
        dest='check_mismatches',
        action='store_false',
        help='Skip the existing mismatch check'
    )
    parser.set_defaults(check_mismatches=True)
    
    args = parser.parse_args()
    
    if args.seed is not None:
        random.seed(args.seed)
    
    atoms = ATOMS[:args.num_atoms]
    mismatch_dir = Path(args.binary).parent.parent.parent / 'examples' / 'mismatches'
    mismatch_dir.mkdir(parents=True, exist_ok=True)

    def parse_partition_file(path):
        inputs = []
        outputs = []
        with open(path, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('.inputs:'):
                    inputs = line.split(':', 1)[1].split()
                elif line.startswith('.outputs:'):
                    outputs = line.split(':', 1)[1].split()
        return inputs, outputs

    def mismatch_index_from_path(path):
        stem = path.stem
        prefix = 'mismatch'
        if stem.startswith(prefix):
            suffix = stem[len(prefix):]
            try:
                return int(suffix)
            except ValueError:
                return None
        return None

    def check_existing_mismatches():
        mismatch_files = sorted(
            mismatch_dir.glob('mismatch*.ltlfplus'),
            key=lambda p: (mismatch_index_from_path(p) is None, mismatch_index_from_path(p) or 0, p.name)
        )
        if not mismatch_files:
            return True

        print('Checking existing mismatches...')
        still_disagree = []
        resolved = []

        for formula_path in mismatch_files:
            idx = mismatch_index_from_path(formula_path)
            part_path = formula_path.with_suffix('.part')
            label = f" mismatch {idx}" if idx is not None else f" {formula_path.stem}"

            if not part_path.exists():
                print(f"  Missing partition for{label}; aborting")
                return False

            formula = formula_path.read_text().strip()
            inputs, outputs = parse_partition_file(part_path)

            result = test_single_formula(
                args.binary,
                formula,
                inputs,
                outputs,
                args.verbose,
                timeout=args.timeout
            )

            if result['error']:
                print(f"  Existing mismatch{label} triggers an error")
                return False
            if result.get('timeout'):
                timeouts = ', '.join(result.get('timeout_solvers', [])) or 'unknown solver'
                print(f"  Existing mismatch{label} timed out ({timeouts})")
                return False
            if result['agree']:
                resolved.append(label.strip())
            else:
                still_disagree.append(label.strip())

        if still_disagree:
            print('Existing mismatches still disagree: ' + ', '.join(still_disagree))
            print('Resolve existing mismatches before generating new ones.')
            return False

        if resolved:
            print(f"All existing mismatches resolved ({len(resolved)} cases).")
        else:
            print('No existing mismatches found.')
        return True

    if args.check_mismatches:
        if not check_existing_mismatches():
            print('Mismatch check failed; aborting new test run.')
            return 1
    
    if not args.shallow:
        print(f"Testing obligation simplification")
        print(f"Binary: {args.binary}")
        print(f"Atoms: {atoms}")
        print(f"Number of tests: {args.num_tests}")
        print(f"Conjuncts: {args.num_conjuncts}, Depth: {args.depth}")
        print()
    
    def run_tests_live():
        def _runner(stdscr):
            curses.curs_set(0)
            stdscr.nodelay(False)
            results = []
            num_agree = num_disagree = num_error = num_timeout = 0
            for i in range(args.num_tests):
                formula = random_obligation_formula(
                    num_conjuncts=args.num_conjuncts,
                    depth=args.depth,
                    atoms=atoms
                )
                inputs, outputs = random_partition(atoms)
                result = test_single_formula(
                    args.binary, formula, inputs, outputs, args.verbose, timeout=args.timeout
                )
                results.append(result)
                if result['error']:
                    num_error += 1
                elif result.get('timeout'):
                    num_timeout += 1
                elif result['agree']:
                    num_agree += 1
                else:
                    num_disagree += 1
                done = i + 1
                fail_rate = num_disagree / done * 100
                stdscr.clear()
                stdscr.addstr(0, 0, f"Test {done}/{args.num_tests}")
                stdscr.addstr(1, 0, f"Agree: {num_agree}")
                stdscr.addstr(2, 0, f"Disagree: {num_disagree}")
                stdscr.addstr(3, 0, f"Timeouts: {num_timeout}")
                stdscr.addstr(4, 0, f"Errors: {num_error}")
                stdscr.addstr(5, 0, f"Failure rate: {fail_rate:.2f}%")
                stdscr.addstr(7, 0, "Press Ctrl+C to abort")
                stdscr.refresh()
            return results, num_agree, num_disagree, num_error, num_timeout
        return curses.wrapper(_runner)
    
    if args.live:
        results, num_agree, num_disagree, num_error, num_timeout = run_tests_live()
    else:
        results = []
        num_agree = 0
        num_disagree = 0
        num_error = 0
        num_timeout = 0
        
        for i in range(args.num_tests):
            if not args.shallow:
                print(f"Test {i+1}/{args.num_tests}...", end=" ", flush=True)
            
            # Generate random formula and partition
            formula = random_obligation_formula(
                num_conjuncts=args.num_conjuncts,
                depth=args.depth,
                atoms=atoms
            )
            inputs, outputs = random_partition(atoms)
            
            # Run test
            result = test_single_formula(
                args.binary, formula, inputs, outputs, args.verbose, timeout=args.timeout
            )
            results.append(result)
            
            if result['error']:
                if not args.shallow:
                    print("ERROR")
                num_error += 1
            elif result.get('timeout'):
                if not args.shallow:
                    print("TIMEOUT")
                num_timeout += 1
            elif result['agree']:
                if not args.shallow:
                    # Obligation result equals at least one baseline; show that realizability
                    print(f"AGREE (realizable_oblig={result['realizable_oblig']})")
                num_agree += 1
            else:
                if not args.shallow:
                    print(f"DISAGREE! oblig={result['realizable_oblig']} vs baselines {[b[2] for b in result['baseline']]}")
                    print(f"  Formula: {formula}")
                    print(f"  Inputs: {inputs}, Outputs: {outputs}")
                num_disagree += 1
    
    # Dump all mismatches to file, sorted by formula length
    disagreements = [r for r in results if not r['error'] and not r.get('timeout') and not r['agree']]
    disagreements.sort(key=lambda r: len(r['formula']))
    with open('mismatch.txt', 'w') as f:
        for r in disagreements:
            f.write(f"Formula: {r['formula']}\n")
            f.write(f".inputs: {' '.join(r['inputs'])}\n")
            f.write(f".outputs: {' '.join(r['outputs'])}\n")
            f.write(f"oblig={r['realizable_oblig']}, baselines={[b[2] for b in r['baseline']]}\n")
            f.write("\n")
    
    # Dump to examples/mismatches folder
    # Path to visualize_dfa.py script
    script_dir = Path(__file__).parent
    visualize_script = script_dir / 'visualize_dfa.py'
    
    existing_indices = []
    for existing_file in mismatch_dir.glob('mismatch*.ltlfplus'):
        idx = mismatch_index_from_path(existing_file)
        if idx is not None:
            existing_indices.append(idx)
    next_mismatch_index = max(existing_indices) + 1 if existing_indices else 0

    for offset, r in enumerate(disagreements):
        mismatch_idx = next_mismatch_index + offset
        formula_file = mismatch_dir / f"mismatch{mismatch_idx}.ltlfplus"
        part_file = mismatch_dir / f"mismatch{mismatch_idx}.part"
        with open(formula_file, 'w') as f:
            f.write(r['formula'] + "\n")
        with open(part_file, 'w') as f:
            f.write(f".inputs: {' '.join(r['inputs'])}\n")
            f.write(f".outputs: {' '.join(r['outputs'])}\n")
        
        # Extract DFA dump from obligation solver output and generate PNG
        oblig_output = r.get('output_oblig', '')
        if '===PYDFA_BEGIN===' in oblig_output and '===PYDFA_END===' in oblig_output:
            dfa_dump_file = mismatch_dir / f"mismatch{mismatch_idx}_dfa.txt"
            png_file = mismatch_dir / f"mismatch{mismatch_idx}_dfa.png"
            with open(dfa_dump_file, 'w') as f:
                f.write(oblig_output)
            try:
                result = subprocess.run(
                    ['python3', str(visualize_script), str(dfa_dump_file), '-o', str(png_file)],
                    capture_output=True,
                    text=True,
                    timeout=30
                )
                # Check for weakness warning
                if 'WARNING: DFA IS NOT WEAK' in result.stderr:
                    r['not_weak'] = True
                    print(f"  WARNING: mismatch{mismatch_idx} has non-weak DFA!")
            except Exception as e:
                pass  # Ignore visualization errors
        else:
            print("Cannot produce DFA visualization: DFA dump not found in output.")
            print(oblig_output)
            
            sys.exit(-1)

    if disagreements:
        print(f"New mismatches start at index {next_mismatch_index}.")
    
    # Count non-weak DFAs
    num_not_weak = sum(1 for r in disagreements if r.get('not_weak', False))
    
    if args.shallow:
        print(f"{num_disagree}")
        if num_not_weak > 0:
            print(f"WARNING: {num_not_weak} disagreements have non-weak DFAs!")
        if args.show_smallest and num_disagree > 0:
            print("Smallest disagreements:")
            for r in disagreements[:10]:
                print(f"  {r['formula']}")
                print(f"    .inputs: {' '.join(r['inputs'])}")
                print(f"    .outputs: {' '.join(r['outputs'])}")
                print(f"     oblig={r['realizable_oblig']}")
                if r.get('not_weak'):
                    print(f"    WARNING: DFA IS NOT WEAK!")
        if args.num_tests > 0:
            fail_rate = num_disagree / args.num_tests * 100
            print(f"Timeouts: {num_timeout}")
            print(f"Failure rate: {fail_rate:.1f}%")
        else:
            print(f"Timeouts: {num_timeout}")
            print(f"Failure rate: N/A (no tests run)")
    else:
        print()
        print("=" * 60)
        print(f"Results: {num_agree} agree, {num_disagree} disagree, {num_timeout} timeouts, {num_error} errors")
        if args.num_tests > 0:
            agreement_rate = num_agree / args.num_tests * 100
            print(f"Agreement rate: {agreement_rate:.1f}%")
            fail_rate = num_disagree / args.num_tests * 100
            print(f"Failure rate: {fail_rate:.1f}%")
        else:
            print("Agreement rate: N/A (no tests run)")
            print("Failure rate: N/A (no tests run)")
        
        if num_disagree > 0:
            print()
            print("Disagreements:")
            for r in results:
                if not r['error'] and not r['agree']:
                    print(f"  Formula: {r['formula']}")
                    print(f"  Inputs: {r['inputs']}, Outputs: {r['outputs']}")
                    #print(f"  Normal: {r['realizable_normal']}, Oblig: {r['realizable_oblig']}")
                    print(f"  Normal output: {r.get('output_normal', 'N/A')}")
                    print(f"  Oblig output: {r.get('output_oblig', 'N/A')}")
                    print()

    # Optional runtime benchmark with harder formulas
    if args.benchmark_runtime > 0:
        harder_depth = max(args.depth + 1, args.depth)
        harder_conj = max(args.num_conjuncts + 1, args.num_conjuncts)
        bench_atoms = ATOMS[:min(len(ATOMS), args.num_atoms + 2)]
        if not args.shallow:
            print()
            print("=" * 60)
            print(f"Runtime benchmark (count={args.benchmark_runtime})")
            print(f"Harder formulas: conjuncts={harder_conj}, depth={harder_depth}, atoms={bench_atoms}")
            print(f"Using solver g=0 (default), timeout={args.timeout}s")
        
        # Create timeouts directory
        timeout_dir = Path(args.binary).parent.parent.parent / 'examples' / 'timeouts'
        timeout_dir.mkdir(parents=True, exist_ok=True)
        
        times_new = []
        times_old = []
        bench_fail = 0
        bench_timeouts = []
        bench_start = time.perf_counter()
        for bench_idx in range(args.benchmark_runtime):
            formula = random_obligation_formula(
                num_conjuncts=harder_conj,
                depth=harder_depth,
                atoms=bench_atoms
            )
            inputs, outputs = random_partition(bench_atoms)
            with tempfile.TemporaryDirectory() as tmpdir:
                formula_file = os.path.join(tmpdir, "bench.ltlfplus")
                part_file = os.path.join(tmpdir, "bench.part")
                write_formula_file(formula, formula_file)
                write_partition_file(inputs, outputs, part_file)

                # New (simplification on)
                t0 = time.perf_counter()
                status_new, _, _ = run_solver(args.binary, formula_file, part_file, 1, solver_id="0", timeout=args.timeout)
                t1 = time.perf_counter()
                # Old (simplification off)
                t2 = time.perf_counter()
                status_old, _, _ = run_solver(args.binary, formula_file, part_file, 0, solver_id="0", timeout=args.timeout)
                t3 = time.perf_counter()

                # Track timeouts and save them
                timeout_info = []
                if status_new == "timeout":
                    timeout_info.append("new(simplification=on)")
                if status_old == "timeout":
                    timeout_info.append("old(simplification=off)")
                
                if timeout_info:
                    # Save timeout formula
                    timeout_formula_file = timeout_dir / f"timeout{bench_idx}.ltlfplus"
                    timeout_part_file = timeout_dir / f"timeout{bench_idx}.part"
                    timeout_info_file = timeout_dir / f"timeout{bench_idx}.txt"
                    
                    with open(timeout_formula_file, 'w') as f:
                        f.write(formula + "\n")
                    with open(timeout_part_file, 'w') as f:
                        f.write(f".inputs: {' '.join(inputs)}\n")
                        f.write(f".outputs: {' '.join(outputs)}\n")
                    with open(timeout_info_file, 'w') as f:
                        f.write(f"Timeout occurred in benchmark run {bench_idx}\n")
                        f.write(f"Formula: {formula}\n")
                        f.write(f"Inputs: {' '.join(inputs)}\n")
                        f.write(f"Outputs: {' '.join(outputs)}\n")
                        f.write(f"Timeout in: {', '.join(timeout_info)}\n")
                        f.write(f"Timeout value: {args.timeout}s\n")
                    
                    bench_timeouts.append({
                        'index': bench_idx,
                        'formula': formula,
                        'inputs': inputs,
                        'outputs': outputs,
                        'timeout_solvers': timeout_info
                    })
                    bench_fail += 1
                elif status_new == "ok":
                    times_new.append(t1 - t0)
                else:
                    bench_fail += 1
                
                if status_old == "ok":
                    times_old.append(t3 - t2)
                elif status_old != "timeout":
                    bench_fail += 1

        bench_total = time.perf_counter() - bench_start

        if times_new and times_old:
            avg_new = sum(times_new) / len(times_new)
            avg_old = sum(times_old) / len(times_old)
            min_new, max_new = min(times_new), max(times_new)
            min_old, max_old = min(times_old), max(times_old)
            if not args.shallow:
                print(f"Avg runtime (new, simplification on):  {avg_new:.3f}s over {len(times_new)} runs")
                print(f"Avg runtime (old, simplification off): {avg_old:.3f}s over {len(times_old)} runs")
                print(f"Speedup (old/new): {avg_old / avg_new:.2f}x")
                print(f"Range new: {min_new:.3f}s - {max_new:.3f}s")
                print(f"Range old: {min_old:.3f}s - {max_old:.3f}s")
                print(f"Benchmark wall-clock: {bench_total:.3f}s for {args.benchmark_runtime} instances (failures/timeouts: {bench_fail})")
                if bench_timeouts:
                    print(f"Timeouts: {len(bench_timeouts)} formulas timed out:")
                    for to in bench_timeouts:
                        print(f"  timeout{to['index']}: {', '.join(to['timeout_solvers'])}")
            else:
                print(f"Benchmark avg new={avg_new:.3f}s old={avg_old:.3f}s speedup={avg_old / avg_new:.2f}x "
                      f"wall={bench_total:.3f}s fails={bench_fail}")
                if bench_timeouts:
                    print(f"Timeouts: {len(bench_timeouts)}")
        else:
            print(f"Runtime benchmark skipped (no successful runs). Wall-clock: {bench_total:.3f}s, failures/timeouts: {bench_fail}")
            if bench_timeouts:
                print(f"Timeouts: {len(bench_timeouts)} formulas timed out:")
                for to in bench_timeouts:
                    print(f"  timeout{to['index']}: {', '.join(to['timeout_solvers'])}")
    
    return 0 if num_disagree == 0 and num_error == 0 else 1

if __name__ == '__main__':
    exit(main())

