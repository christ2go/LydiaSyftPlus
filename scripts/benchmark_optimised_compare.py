#!/usr/bin/env python3
"""
Benchmark script comparing EL vs Optimised Buchi solvers (classic, Piterman, weak-game, CoBuchi modes).

Runs solvers on each example:
- EL solver (-g 0)
- Optimised Classic (-g 1 --obligation-simplification 1 -b cl)
- Optimised Piterman (-g 1 --obligation-simplification 1 -b pm)
- Optimised Weak-game/SCC (-g 1 --obligation-simplification 1 -b wg)
- Optimised CoBuchi (-g 1 --obligation-simplification 1 -b cb)

Plots runtime results with timeout line. Times are in seconds with millisecond precision.
"""
from scipy.stats import t
import math

import subprocess
import os
import time
import argparse
import json
import math
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

def run_solver(binary, formula_file, partition_file, solver_id, obligation_simplification, buechi_mode, timeout_sec, num_runs=1):
    """
    Run a solver on a formula multiple times and return statistics.
    
    Args:
        num_runs: Number of times to run the solver (default: 1)
    
    Returns:
        dict with keys: 'status', 'mean', 'stddev', 'conf_interval', 'runs'
        - status: 'ok', 'timeout', 'error', or 'partial' (some runs succeeded)
        - mean: average runtime in seconds
        - stddev: standard deviation in seconds
        - conf_interval: 95% confidence interval half-width in seconds
        - runs: list of individual runtimes in seconds
    """
    cmd = [
        binary,
        "-i", formula_file,
        "-p", partition_file,
        "-s", "0",
        "-g", str(solver_id),
        "--obligation-simplification", str(obligation_simplification)
    ]
    
    # Add buechi-mode flag if specified (only for optimised solvers)
    if buechi_mode is not None:
        cmd.extend(["-b", buechi_mode])
    
    runtimes = []
    error_count = 0
    timeout_count = 0
    
    for run_idx in range(num_runs):
        start_time = time.time()
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout_sec
            )
            elapsed = (time.time() - start_time)  # seconds
            
            if result.returncode == 0:
                runtimes.append(elapsed)
            else:
                error_count += 1
                # Don't include error runs in statistics
        except subprocess.TimeoutExpired:
            timeout_count += 1
            # Don't include timeout runs in statistics
    
    # Determine overall status
    if timeout_count > 0:
        status = 'timeout'
    elif error_count > 0 and len(runtimes) == 0:
        status = 'error'
    elif error_count > 0 and len(runtimes) > 0:
        status = 'partial'
    else:
        status = 'ok'
    
    # Compute statistics (values are in seconds)
    if len(runtimes) == 0:
        return {
            'status': status,
            'mean': 0.0,
            'stddev': 0.0,
            'conf_interval': 0.0,
            'runs': []
        }
    
    mean = sum(runtimes) / len(runtimes)
    
    if len(runtimes) > 1:
        # Sample standard deviation
        variance = sum((x - mean) ** 2 for x in runtimes) / (len(runtimes) - 1)
        stddev = math.sqrt(variance)

        t_crit = t.ppf(0.975, len(runtimes) - 1)  # two-sided 95%
        conf_interval = t_crit * stddev / math.sqrt(len(runtimes))
    else:
        stddev = 0.0
        conf_interval = 0.0
    
    return {
        'status': status,
        'mean': mean,
        'stddev': stddev,
        'conf_interval': conf_interval,
        'runs': runtimes
    }

def benchmark_examples(examples_dir, timeout_sec=60, skip_set=None, max_example=None, num_runs=1, pattern_prefix="pattern"):
    """Run benchmarks on all pattern examples.
    
    Args:
        examples_dir: Directory containing pattern files
        timeout_sec: Timeout in seconds for each run
        skip_set: Set of solver names to skip
        max_example: Maximum pattern number to test (inclusive), None for all
        num_runs: Number of runs to average for each example
        pattern_prefix: Prefix for pattern files (e.g., "pattern", "chained", "dual")
    """
    binary = find_binary()

    # Find all pattern files
    pattern_files = sorted(
        [f for f in os.listdir(examples_dir) if f.startswith(f"{pattern_prefix}_") and f.endswith(".ltlfplus")],
        key=lambda x: int(x.replace(f"{pattern_prefix}_", "").replace(".ltlfplus", ""))
    )
    
    # Filter by max_example if specified
    if max_example is not None:
        pattern_files = [f for f in pattern_files 
                        if int(f.replace(f"{pattern_prefix}_", "").replace(".ltlfplus", "")) <= max_example]

    results = {
        'el': [],           # EL solver (-g 0)
        'opt_classic': [],  # Optimised Classic (-g 1 --obligation-simplification 1 -b cl)
        'opt_piterman': [],  # Optimised Piterman (-g 1 --obligation-simplification 1 -b pm)
        'opt_weak': [],     # Optimised Weak-game (SCC) (-g 1 --obligation-simplification 1 -b wg)
        'opt_cobuchi': []   # Optimised Cobuchi (-g 1 --obligation-simplification 1 -b cb)
    }

    print(f"Found {len(pattern_files)} pattern files")
    if max_example is not None:
        print(f"Limiting to patterns up to n={max_example}")
    print(f"Timeout: {timeout_sec} seconds")
    print(f"Runs per example: {num_runs}")
    print(f"Binary: {binary}\n")

    if skip_set is None:
        skip_set = set()

    for pattern_file in pattern_files:
        n = int(pattern_file.replace(f"{pattern_prefix}_", "").replace(".ltlfplus", ""))
        formula_file = os.path.join(examples_dir, pattern_file)
        partition_file = os.path.join(examples_dir, pattern_file.replace(".ltlfplus", ".part"))
        
        if not os.path.exists(partition_file):
            print(f"Warning: Partition file not found for {pattern_file}")
            continue
        
        print(f"Testing {pattern_prefix}_{n}...", end=" ", flush=True)
        
        # Run EL solver (may be skipped)
        if 'el' in skip_set:
            results['el'].append((n, {'status': 'skipped', 'mean': 0.0, 'stddev': 0.0, 'conf_interval': 0.0, 'runs': []}))
            print(f"EL: skipped", end=" ", flush=True)
        else:
            stats = run_solver(binary, formula_file, partition_file, 0, 0, None, timeout_sec, num_runs)
            results['el'].append((n, stats))
            if stats['status'] == 'ok':
                print(f"EL: {stats['mean']:.3f}±{stats['conf_interval']:.3f}s", end=" ", flush=True)
            else:
                print(f"EL: {stats['status']}", end=" ", flush=True)
        
        # Run Optimised Classic (may be skipped)
        if 'classic' in skip_set or 'opt_classic' in skip_set:
            results['opt_classic'].append((n, {'status': 'skipped', 'mean': 0.0, 'stddev': 0.0, 'conf_interval': 0.0, 'runs': []}))
            print(f"Opt-CL: skipped", end=" ", flush=True)
        else:
            stats = run_solver(binary, formula_file, partition_file, 1, 1, "cl", timeout_sec, num_runs)
            results['opt_classic'].append((n, stats))
            if stats['status'] == 'ok':
                print(f"Opt-CL: {stats['mean']:.3f}±{stats['conf_interval']:.3f}s", end=" ", flush=True)
            else:
                print(f"Opt-CL: {stats['status']}", end=" ", flush=True)
        
        # Run Optimised Piterman (may be skipped)
        if 'piterman' in skip_set or 'opt_piterman' in skip_set or 'pm' in skip_set:
            results['opt_piterman'].append((n, {'status': 'skipped', 'mean': 0.0, 'stddev': 0.0, 'conf_interval': 0.0, 'runs': []}))
            print(f"Opt-PM: skipped", end=" ", flush=True)
        else:
            stats = run_solver(binary, formula_file, partition_file, 1, 1, "pm", timeout_sec, num_runs)
            results['opt_piterman'].append((n, stats))
            if stats['status'] == 'ok':
                print(f"Opt-PM: {stats['mean']:.3f}±{stats['conf_interval']:.3f}s", end=" ", flush=True)
            else:
                print(f"Opt-PM: {stats['status']}", end=" ", flush=True)
        
        # Run Optimised Weak-game (SCC) solver
        if 'weak' in skip_set or 'opt_weak' in skip_set or 'wg' in skip_set:
            results['opt_weak'].append((n, {'status': 'skipped', 'mean': 0.0, 'stddev': 0.0, 'conf_interval': 0.0, 'runs': []}))
            print(f"Opt-WG: skipped", end=" ", flush=True)
        else:
            stats = run_solver(binary, formula_file, partition_file, 1, 1, "wg", timeout_sec, num_runs)
            results['opt_weak'].append((n, stats))
            if stats['status'] == 'ok':
                print(f"Opt-WG: {stats['mean']:.3f}±{stats['conf_interval']:.3f}s", end=" ", flush=True)
            else:
                print(f"Opt-WG: {stats['status']}", end=" ", flush=True)

        # Run Optimised CoBuchi (may be skipped)
        if 'cobuchi' in skip_set or 'opt_cobuchi' in skip_set or 'cb' in skip_set:
            results['opt_cobuchi'].append((n, {'status': 'skipped', 'mean': 0.0, 'stddev': 0.0, 'conf_interval': 0.0, 'runs': []}))
            print(f"Opt-CB: skipped")
        else:
            stats = run_solver(binary, formula_file, partition_file, 1, 1, "cb", timeout_sec, num_runs)
            results['opt_cobuchi'].append((n, stats))
            if stats['status'] == 'ok':
                print(f"Opt-CB: {stats['mean']:.3f}±{stats['conf_interval']:.3f}s")
            else:
                print(f"Opt-CB: {stats['status']}")
    
    return results

def plot_results(results, timeout_sec, output_file):
    """Plot the benchmark results (times are in seconds)."""
    if not HAS_MATPLOTLIB:
        print("Skipping plot generation (matplotlib not available)")
        return

    # Extract data
    n_values = [r[0] for r in results['el']]
    timeout_s = float(timeout_sec)

    # For plotting, treat errors as timeouts (use timeout value instead of actual runtime)
    el_times = []
    opt_classic_times = []
    opt_piterman_times = []
    opt_weak_times = []

    for r in results['el']:
        stats = r[1]
        if stats['status'] == 'skipped':
            el_times.append(np.nan)
        elif stats['status'] in ['timeout', 'error']:
            el_times.append(timeout_s)
        else:
            el_times.append(stats['mean'])

    for r in results['opt_classic']:
        stats = r[1]
        if stats['status'] == 'skipped':
            opt_classic_times.append(np.nan)
        elif stats['status'] in ['timeout', 'error']:
            opt_classic_times.append(timeout_s)
        else:
            opt_classic_times.append(stats['mean'])

    for r in results['opt_piterman']:
        stats = r[1]
        if stats['status'] == 'skipped':
            opt_piterman_times.append(np.nan)
        elif stats['status'] in ['timeout', 'error']:
            opt_piterman_times.append(timeout_s)
        else:
            opt_piterman_times.append(stats['mean'])

    for r in results.get('opt_weak', []):
        stats = r[1]
        if stats['status'] == 'skipped':
            opt_weak_times.append(np.nan)
        elif stats['status'] in ['timeout', 'error']:
            opt_weak_times.append(timeout_s)
        else:
            opt_weak_times.append(stats['mean'])

    # Create figure
    plt.figure(figsize=(12, 8))

    # Plot timeout line (seconds)
    plt.axhline(y=timeout_s, color='red', linestyle='--', linewidth=2, label=f'Timeout ({timeout_sec}s)')

    # Plot solver results
    plt.plot(n_values, el_times, 'o-', label='EL Solver (-g 0)', linewidth=2, markersize=8, color='tab:blue')
    plt.plot(n_values, opt_classic_times, 's-', label='Optimised Classic (-b cl)', linewidth=2, markersize=8, color='tab:green')
    plt.plot(n_values, opt_piterman_times, '^-', label='Optimised Piterman (-b pm)', linewidth=2, markersize=8, color='tab:orange')
    plt.plot(n_values, opt_weak_times, 'd-', label='Optimised WeakGame (-b wg)', linewidth=2, markersize=8, color='tab:purple')

    # Mark timeouts and errors (plot above timeout line)
    timeout_offset = timeout_s * 1.1  # 10% above timeout line
    for i, (n, stats) in enumerate(results['el']):
        if stats['status'] == 'timeout':
            plt.plot(n, timeout_offset, 'o', markersize=12, markerfacecolor='red', markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif stats['status'] == 'error':
            plt.plot(n, timeout_offset, 'x', markersize=12, markeredgewidth=3, color='red', zorder=10)

    for i, (n, stats) in enumerate(results['opt_classic']):
        if stats['status'] == 'timeout':
            plt.plot(n, timeout_offset, 's', markersize=12, markerfacecolor='red', markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif stats['status'] == 'error':
            plt.plot(n, timeout_offset, 'x', markersize=12, markeredgewidth=3, color='red', zorder=10)

    for i, (n, stats) in enumerate(results['opt_piterman']):
        if stats['status'] == 'timeout':
            plt.plot(n, timeout_offset, '^', markersize=12, markerfacecolor='red', markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif stats['status'] == 'error':
            plt.plot(n, timeout_offset, 'x', markersize=12, markeredgewidth=3, color='red', zorder=10)

    for i, (n, stats) in enumerate(results.get('opt_weak', [])):
        if stats['status'] == 'timeout':
            plt.plot(n, timeout_offset, 'd', markersize=12, markerfacecolor='red', markeredgewidth=2, markeredgecolor='black', zorder=10)
        elif stats['status'] == 'error':
            plt.plot(n, timeout_offset, 'x', markersize=12, markeredgewidth=3, color='red', zorder=10)

    plt.xlabel('Pattern Size (n)', fontsize=14)
    plt.ylabel('Runtime (s)', fontsize=14)
    plt.title('∃-Pattern Benchmark: EL vs Optimised Solvers', fontsize=16)
    plt.legend(fontsize=12)
    plt.grid(True, alpha=0.3)
    plt.yscale('log')  # Use log scale for better visualization

    # Set y-axis to show timeout clearly (use nan-safe max). Values are seconds now.
    max_time = np.nanmax([el_times, opt_classic_times, opt_piterman_times, opt_weak_times])
    if np.isnan(max_time):
        max_time = timeout_s
    # bottom should be small (allow sub-second values)
    try:
        min_positive_vals = [np.nanmin([v for v in arr if (not np.isnan(v)) and (v > 0)]) for arr in [el_times, opt_classic_times, opt_piterman_times, opt_weak_times]]
        min_positive = min([v for v in min_positive_vals if not math.isnan(v)])
        bottom = max(min_positive * 0.1, 1e-6)
    except Exception:
        bottom = 1e-6

    plt.ylim(bottom=bottom, top=max(max_time * 1.5, timeout_s * 1.5))

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\nPlot saved to: {output_file}")

def export_json(results, output_file):
    """Export benchmark results to JSON format."""
    # Convert results to JSON-serializable format
    json_data = {
        'solvers': {}
    }
    
    for solver_name, solver_results in results.items():
        json_data['solvers'][solver_name] = []
        for n, stats in solver_results:
            json_data['solvers'][solver_name].append({
                'pattern_n': n,
                'status': stats['status'],
                'mean_s': stats['mean'],
                'stddev_s': stats['stddev'],
                'conf_interval_95_s': stats['conf_interval'],
                'individual_runs_s': stats['runs']
            })
    
    with open(output_file, 'w') as f:
        json.dump(json_data, f, indent=2)
    
    print(f"JSON results saved to: {output_file}")

def export_latex_table(results, output_file, timeout_sec):
    """Export benchmark results as a LaTeX table (times in seconds)."""
    timeout_s = float(timeout_sec)

    with open(output_file, 'w') as f:
        # Write table header
        f.write("\\begin{table}[htbp]\n")
        f.write("\\centering\n")
        f.write("\\begin{tabular}{r|ccccc}\n")
        f.write("\\hline\n")
        f.write("$n$ & EL & Classic & Piterman & WeakGame & CoBuchi \\\\\n")
        f.write("\\hline\n")

        # Get all pattern numbers
        n_values = [r[0] for r in results['el']]

        # Define format_cell function
        def format_cell(stats):
            if stats['status'] == 'skipped':
                return "---"
            elif stats['status'] == 'timeout':
                return "TO"
            elif stats['status'] == 'error':
                return "ERR"
            elif stats['status'] == 'partial':
                # Show mean in bold with asterisk, conf interval below
                if stats['conf_interval'] > 0:
                    return f"\\textbf{{{stats['mean']:.3f}s}}$^*$  {{\\scriptsize $\\pm${stats['conf_interval']:.3f}s}}"
                else:
                    return f"\\textbf{{{stats['mean']:.3f}s}}$^*$"
            else:
                # Format with mean in bold on first line, ± conf_interval below
                if stats['conf_interval'] > 0:
                    return f"\\textbf{{{stats['mean']:.3f}s}}  {{\\scriptsize $\\pm${stats['conf_interval']:.3f}s}}"
                else:
                    return f"\\textbf{{{stats['mean']:.3f}s}}"

        # Write data rows
        for i, n in enumerate(n_values):
            el_stats = results['el'][i][1]
            classic_stats = results['opt_classic'][i][1]
            piterman_stats = results['opt_piterman'][i][1]
            weak_stats = results.get('opt_weak', [(None, {'status':'skipped'})])[i][1] if i < len(results.get('opt_weak', [])) else {'status':'skipped'}
            cobuchi_stats = results['opt_cobuchi'][i][1]

            f.write(f"{n} & {format_cell(el_stats)} & {format_cell(classic_stats)} & {format_cell(piterman_stats)} & {format_cell(weak_stats)} & {format_cell(cobuchi_stats)} \\\\\n")

        # Write table footer
        f.write("\\hline\n")
        f.write("\\end{tabular}\n")
        f.write("\\caption{Benchmark results (runtime in s with 95\\% confidence interval). TO = timeout, ERR = error, $^*$ = partial success.}\n")
        f.write("\\label{tab:benchmark_results}\n")
        f.write("\\end{table}\n")

    print(f"LaTeX table saved to: {output_file}")

def main():
    parser = argparse.ArgumentParser(description='Benchmark ∃-Pattern examples (EL vs Optimised solvers)')
    parser.add_argument('--timeout', type=int, default=60, help='Timeout in seconds (default: 60)')
    parser.add_argument('--output', type=str, default='optimised_compare_benchmark.png', 
                       help='Output plot file (default: optimised_compare_benchmark.png)')
    parser.add_argument('--json', type=str, default='benchmark_results.json',
                       help='Output JSON file (default: benchmark_results.json)')
    parser.add_argument('--latex', type=str, default='benchmark_table.tex',
                       help='Output LaTeX table file (default: benchmark_table.tex)')
    parser.add_argument('--examples-dir', type=str, default=None,
                       help='Directory containing pattern files (default: examples/frompaper)')
    parser.add_argument('--skip', type=str, default='',
                        help='Comma-separated list of solvers to skip: el,classic,piterman,weak,cobuchi (e.g. --skip=el)')
    parser.add_argument('--max-example', type=int, default=None,
                       help='Maximum example number to run (e.g. --max-example=20 runs pattern_1 to pattern_20)')
    parser.add_argument('--runs', type=int, default=1,
                       help='Number of runs to average for each example (default: 1)')
    parser.add_argument('--pattern', type=str, default='pattern',
                       help='Pattern prefix for files (default: pattern; use chained, dual, nested, etc.)')
    
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
    print("∃-Pattern Benchmark: EL vs Optimised Solvers")
    print("=" * 60)
    
    # Parse skip list into a set for easier checks
    skip_set = set()
    if args.skip:
        for token in args.skip.split(','):
            tok = token.strip().lower()
            if tok:
                skip_set.add(tok)

    results = benchmark_examples(examples_dir, args.timeout, skip_set=skip_set, max_example=args.max_example, num_runs=args.runs, pattern_prefix=args.pattern)
    
    # Export JSON
    print("\n" + "=" * 60)
    print("Exporting JSON results...")
    print("=" * 60)
    export_json(results, args.json)
    
    # Export LaTeX table
    print("\n" + "=" * 60)
    print("Generating LaTeX table...")
    print("=" * 60)
    export_latex_table(results, args.latex, args.timeout)
    
    # Plot results
    print("\n" + "=" * 60)
    print("Generating plot...")
    print("=" * 60)
    plot_results(results, args.timeout, args.output)
    
    # Print summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    
    for solver_name, solver_key in [
        ('EL', 'el'), 
        ('Classic Buchi', 'opt_classic'), 
        ('Piterman\'s new', 'opt_piterman'),
        ('Weak-game (SCC)', 'opt_weak'),
        ('CoBuchi', 'opt_cobuchi')
    ]:
        solver_results = results[solver_key]
        timeouts = sum(1 for _, stats in solver_results if stats['status'] == 'timeout')
        errors = sum(1 for _, stats in solver_results if stats['status'] == 'error')
        ok = sum(1 for _, stats in solver_results if stats['status'] == 'ok')
        partial = sum(1 for _, stats in solver_results if stats['status'] == 'partial')
        skipped = sum(1 for _, stats in solver_results if stats['status'] == 'skipped')
        
        ok_stats = [stats for _, stats in solver_results if stats['status'] == 'ok']
        if ok_stats:
            avg_time = sum(s['mean'] for s in ok_stats) / len(ok_stats)
            avg_stddev = sum(s['stddev'] for s in ok_stats) / len(ok_stats)
        else:
            avg_time = 0
            avg_stddev = 0
        
        status_parts = []
        if ok > 0:
            status_parts.append(f"{ok} OK")
        if partial > 0:
            status_parts.append(f"{partial} partial")
        if timeouts > 0:
            status_parts.append(f"{timeouts} timeouts")
        if errors > 0:
            status_parts.append(f"{errors} errors")
        if skipped > 0:
            status_parts.append(f"{skipped} skipped")
        
        print(f"{solver_name}: {', '.join(status_parts)}")
        if ok > 0:
            print(f"  Average runtime (OK cases): {avg_time:.3f} ± {avg_stddev:.3f} s")
    
    print("\n" + "=" * 60)
    print(f"Results exported to:")
    print(f"  - Plot: {args.output}")
    print(f"  - JSON: {args.json}")
    print(f"  - LaTeX: {args.latex}")
    print("=" * 60)
    
    return 0

if __name__ == "__main__":
    exit(main())