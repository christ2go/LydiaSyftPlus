#!/usr/bin/env python3
"""
Benchmark script for ∃-Pattern examples from paper.

Runs three solvers on each example:
- EL solver (-g 0)
- MP solver (-g 1)
- Obligation-simplification solver (-g 1 --obligation-simplification 1)

Plots runtime results with timeout line.
"""

import subprocess
import os
import time
import argparse
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Warning: matplotlib not available. Plotting will be disabled.")
    print("Install with: pip install matplotlib numpy")

def find_binary():
    """Find the LydiaSyftEL binary."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    # Try common build locations
    candidates = [
        os.path.join(project_root, "build", "bin", "LydiaSyftEL"),
        os.path.join(project_root, "build", "LydiaSyftEL"),
        os.path.join(project_root, "LydiaSyftEL"),
    ]
    
    for candidate in candidates:
        if os.path.exists(candidate) and os.access(candidate, os.X_OK):
            return candidate
    
    raise FileNotFoundError("Could not find LydiaSyftEL binary. Please build the project first.")

def run_solver(binary, formula_file, partition_file, solver_id, obligation_simplification, timeout_sec):
    """
    Run a solver on a formula.
    
    Returns:
        (status, runtime_ms) where status is 'ok', 'timeout', or 'error'
    """
    cmd = [
        binary,
        "-i", formula_file,
        "-p", partition_file,
        "-s", "0",
        "-g", str(solver_id),
        "--obligation-simplification", str(obligation_simplification)
    ]
    
    start_time = time.time()
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_sec
        )
        elapsed = (time.time() - start_time) * 1000  # Convert to ms
        
        if result.returncode == 0:
            return ('ok', elapsed)
        else:
            return ('error', elapsed)
    except subprocess.TimeoutExpired:
        elapsed = timeout_sec * 1000  # Convert to ms
        return ('timeout', elapsed)

def benchmark_examples(examples_dir, timeout_sec=60, skip_set=None):
    """Run benchmarks on all pattern examples."""
    binary = find_binary()

    # Find all pattern files
    pattern_files = sorted(
        [f for f in os.listdir(examples_dir) if f.startswith("pattern_") and f.endswith(".ltlfplus")],
        key=lambda x: int(x.replace("pattern_", "").replace(".ltlfplus", ""))
    )

    results = {
        'el': [],      # EL solver (-g 0)
        'mp': [],      # MP solver (-g 1)
        'oblig': []    # Obligation-simplification (-g 1 --obligation-simplification 1)
    }

    print(f"Found {len(pattern_files)} pattern files")
    print(f"Timeout: {timeout_sec} seconds")
    print(f"Binary: {binary}\n")

    if skip_set is None:
        skip_set = set()

    for pattern_file in pattern_files:
        n = int(pattern_file.replace("pattern_", "").replace(".ltlfplus", ""))
        formula_file = os.path.join(examples_dir, pattern_file)
        partition_file = os.path.join(examples_dir, pattern_file.replace(".ltlfplus", ".part"))
        
        if not os.path.exists(partition_file):
            print(f"Warning: Partition file not found for {pattern_file}")
            continue
        
        print(f"Testing pattern_{n}...", end=" ", flush=True)
        
        # Run EL solver (may be skipped)
        if 'el' in skip_set:
            results['el'].append((n, 'skipped', 0.0))
            print(f"EL: skipped", end=" ", flush=True)
        else:
            status, runtime = run_solver(binary, formula_file, partition_file, 0, 0, timeout_sec)
            results['el'].append((n, status, runtime))
            print(f"EL: {runtime:.1f}ms ({status})", end=" ", flush=True)
        
        # Run MP solver (may be skipped)
        if 'mp' in skip_set:
            results['mp'].append((n, 'skipped', 0.0))
            print(f"MP: skipped", end=" ", flush=True)
        else:
            status, runtime = run_solver(binary, formula_file, partition_file, 1, 0, timeout_sec)
            results['mp'].append((n, status, runtime))
            print(f"MP: {runtime:.1f}ms ({status})", end=" ", flush=True)
        
        # Run Obligation-simplification solver (may be skipped)
        if 'oblig' in skip_set or 'obligation' in skip_set:
            results['oblig'].append((n, 'skipped', 0.0))
            print(f"Oblig: skipped")
        else:
            status, runtime = run_solver(binary, formula_file, partition_file, 1, 1, timeout_sec)
            results['oblig'].append((n, status, runtime))
            print(f"Oblig: {runtime:.1f}ms ({status})")
    
    return results

def plot_results(results, timeout_sec, output_file):
    """Plot the benchmark results."""
    if not HAS_MATPLOTLIB:
        print("Skipping plot generation (matplotlib not available)")
        return
    
    # Extract data
    n_values = [r[0] for r in results['el']]
    timeout_ms = timeout_sec * 1000
    
    # For plotting, treat errors as timeouts (use timeout value instead of actual runtime)
    el_times = []
    mp_times = []
    oblig_times = []
    
    for r in results['el']:
        if r[1] in ['timeout', 'error']:
            el_times.append(timeout_ms)
        else:
            el_times.append(r[2])
    
    for r in results['mp']:
        if r[1] == 'skipped':
            # use NaN so matplotlib will leave a gap
            mp_times.append(np.nan)
        elif r[1] in ['timeout', 'error']:
            mp_times.append(timeout_ms)
        else:
            mp_times.append(r[2])
    
    for r in results['oblig']:
        if r[1] in ['timeout', 'error']:
            oblig_times.append(timeout_ms)
        else:
            oblig_times.append(r[2])
    
    # Create figure
    plt.figure(figsize=(12, 8))
    
    # Plot timeout line
    plt.axhline(y=timeout_ms, color='red', linestyle='--', linewidth=2, label=f'Timeout ({timeout_sec}s)')
    
    # Plot solver results
    plt.plot(n_values, el_times, 'o-', label='EL Solver (-g 0)', linewidth=2, markersize=8)
    plt.plot(n_values, mp_times, 's-', label='MP Solver (-g 1)', linewidth=2, markersize=8)
    plt.plot(n_values, oblig_times, '^-', label='Obligation-Simplification', linewidth=2, markersize=8)
    
    # Mark timeouts and errors (plot above timeout line)
    timeout_offset = timeout_ms * 1.1  # 10% above timeout line
    for i, (n, status, runtime) in enumerate(results['el']):
        if status == 'timeout':
            plt.plot(n, timeout_offset, 'ro', markersize=12, markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif status == 'error':
            plt.plot(n, timeout_offset, 'rx', markersize=12, markeredgewidth=2, markeredgecolor='black', zorder=10)
    
    for i, (n, status, runtime) in enumerate(results['mp']):
        if status == 'timeout':
            plt.plot(n, timeout_offset, 'rs', markersize=12, markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif status == 'error':
            plt.plot(n, timeout_offset, 'rx', markersize=12, markeredgewidth=2, markeredgecolor='black', zorder=10)
    
    for i, (n, status, runtime) in enumerate(results['oblig']):
        if status == 'timeout':
            plt.plot(n, timeout_offset, 'r^', markersize=12, markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif status == 'error':
            plt.plot(n, timeout_offset, 'rx', markersize=12, markeredgewidth=2, markeredgecolor='black', zorder=10)
    
    plt.xlabel('Pattern Size (n)', fontsize=14)
    plt.ylabel('Runtime (ms)', fontsize=14)
    plt.title('∃-Pattern Benchmark: EL vs MP vs Obligation-Simplification', fontsize=16)
    plt.legend(fontsize=12)
    plt.grid(True, alpha=0.3)
    plt.yscale('log')  # Use log scale for better visualization
    
    # Set y-axis to show timeout clearly
    # Set y-axis to show timeout clearly (use nan-safe max)
    max_time = np.nanmax([el_times, mp_times, oblig_times])
    plt.ylim(bottom=1, top=max(max_time * 1.5, timeout_ms * 1.5))
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\nPlot saved to: {output_file}")

def main():
    parser = argparse.ArgumentParser(description='Benchmark ∃-Pattern examples')
    parser.add_argument('--timeout', type=int, default=60, help='Timeout in seconds (default: 60)')
    parser.add_argument('--output', type=str, default='frompaper_benchmark.png', 
                       help='Output plot file (default: frompaper_benchmark.png)')
    parser.add_argument('--examples-dir', type=str, default=None,
                       help='Directory containing pattern files (default: examples/frompaper)')
    parser.add_argument('--skip', type=str, default='',
                        help='Comma-separated list of solvers to skip: el,mp,oblig (e.g. --skip=el,mp)')
    
    args = parser.parse_args()
    
    # Determine examples directory
    if args.examples_dir:
        examples_dir = args.examples_dir
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(script_dir)
        examples_dir = os.path.join(project_root, "examples", "frompaper")
    
    if not os.path.exists(examples_dir):
        print(f"Error: Examples directory not found: {examples_dir}")
        print("Please run scripts/generate_frompaper.py first to generate examples.")
        return 1
    
    # Run benchmarks
    print("=" * 60)
    print("∃-Pattern Benchmark")
    print("=" * 60)
    # Parse skip list into a set for easier checks
    skip_set = set()
    if args.skip:
        for token in args.skip.split(','):
            tok = token.strip().lower()
            if tok:
                skip_set.add(tok)

    results = benchmark_examples(examples_dir, args.timeout, skip_set=skip_set)
    
    # Plot results
    print("\n" + "=" * 60)
    print("Generating plot...")
    print("=" * 60)
    plot_results(results, args.timeout, args.output)
    
    # Print summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    
    for solver_name, solver_results in [('EL', results['el']), ('MP', results['mp']), ('Obligation', results['oblig'])]:
        timeouts = sum(1 for _, status, _ in solver_results if status == 'timeout')
        errors = sum(1 for _, status, _ in solver_results if status == 'error')
        ok = sum(1 for _, status, _ in solver_results if status == 'ok')
        skipped = sum(1 for _, status, _ in solver_results if status == 'skipped')
        
        ok_times = [r[2] for r in solver_results if r[1] == 'ok']
        if ok_times:
            avg_time = sum(ok_times) / len(ok_times)
        else:
            avg_time = 0
        
    extra = f", {skipped} skipped" if skipped else ""
    print(f"{solver_name}: {ok} OK, {timeouts} timeouts, {errors} errors{extra}")
    if ok > 0:
        print(f"  Average runtime (OK cases): {avg_time:.1f} ms")
    
    return 0

if __name__ == "__main__":
    exit(main())

