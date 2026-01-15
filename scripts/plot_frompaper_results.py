#!/usr/bin/env python3
"""
Plot results from ∃-Pattern benchmark based on provided output.
"""

import matplotlib.pyplot as plt
import re

# Parse the results
results_text = """Testing pattern_1... EL: 3.1ms (ok) MP: 5.6ms (ok) Oblig: 4.0ms (ok)
Testing pattern_2... EL: 3.0ms (ok) MP: 6.2ms (ok) Oblig: 4.3ms (ok)
Testing pattern_3... EL: 3.6ms (ok) MP: 5.9ms (ok) Oblig: 3.7ms (ok)
Testing pattern_4... EL: 6.3ms (ok) MP: 10.3ms (ok) Oblig: 5.2ms (ok)
Testing pattern_5... EL: 4.9ms (ok) MP: 13.4ms (ok) Oblig: 6.7ms (ok)
Testing pattern_6... EL: 7.3ms (ok) MP: 33.6ms (ok) Oblig: 9.4ms (ok)
Testing pattern_7... EL: 11.9ms (ok) MP: 81.5ms (ok) Oblig: 12.4ms (ok)
Testing pattern_8... EL: 11.9ms (ok) MP: 317.9ms (ok) Oblig: 23.7ms (ok)
Testing pattern_9... EL: 22.2ms (ok) MP: 1254.5ms (ok) Oblig: 79.7ms (ok)
Testing pattern_10... EL: 44.5ms (ok) MP: 5316.4ms (ok) Oblig: 388.7ms (ok)
Testing pattern_11... EL: 98.4ms (ok) MP: 23026.5ms (ok) Oblig: 2138.8ms (ok)
Testing pattern_12... EL: 199.8ms (ok) MP: 60000.0ms (timeout) Oblig: 32356.2ms (ok)
Testing pattern_13... EL: 460.5ms (ok) MP: 60000.0ms (timeout) Oblig: 60000.0ms (timeout)
Testing pattern_14... EL: 1015.6ms (ok) MP: 60000.0ms (timeout) Oblig: 60000.0ms (timeout)
Testing pattern_15... EL: 2408.7ms (ok) MP: 60000.0ms (timeout) Oblig: 18357.6ms (error)
Testing pattern_16... EL: 6112.4ms (ok) MP: 60000.0ms (timeout) Oblig: 16166.0ms (error)
Testing pattern_17... EL: 14055.9ms (ok) MP: 60000.0ms (timeout) Oblig: 17117.7ms (error)
Testing pattern_18... EL: 29682.0ms (ok) MP: 60000.0ms (timeout) Oblig: 17450.3ms (error)
Testing pattern_19... EL: 60000.0ms (timeout) MP: 60000.0ms (timeout) Oblig: 17786.4ms (error)
Testing pattern_20... EL: 60000.0ms (timeout) MP: 60000.0ms (timeout) Oblig: 19382.2ms (error)"""

def parse_results(text):
    """Parse the results text into structured data."""
    results = {
        'el': [],
        'mp': [],
        'oblig': []
    }
    
    timeout_sec = 60
    timeout_ms = timeout_sec * 1000
    
    # Pattern to match: Testing pattern_N... EL: Xms (status) MP: Xms (status) Oblig: Xms (status)
    pattern = r'Testing pattern_(\d+)\.\.\. EL: ([\d.]+)ms \(([^)]+)\) MP: ([\d.]+)ms \(([^)]+)\) Oblig: ([\d.]+)ms \(([^)]+)\)'
    
    for line in text.strip().split('\n'):
        match = re.match(pattern, line)
        if match:
            n = int(match.group(1))
            el_time = float(match.group(2))
            el_status = match.group(3)
            mp_time = float(match.group(4))
            mp_status = match.group(5)
            oblig_time = float(match.group(6))
            oblig_status = match.group(7)
            
            results['el'].append((n, el_status, el_time))
            results['mp'].append((n, mp_status, mp_time))
            results['oblig'].append((n, oblig_status, oblig_time))
    
    return results, timeout_sec

def plot_results(results, timeout_sec, output_file='frompaper_benchmark.png'):
    """Plot the benchmark results."""
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
        if r[1] in ['timeout', 'error']:
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
    max_time = max(max(el_times), max(mp_times), max(oblig_times))
    plt.ylim(bottom=1, top=max(max_time * 1.5, timeout_ms * 1.5))
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved to: {output_file}")

def main():
    results, timeout_sec = parse_results(results_text)
    
    print("=" * 60)
    print("Parsed Results Summary")
    print("=" * 60)
    
    for solver_name, solver_results in [('EL', results['el']), ('MP', results['mp']), ('Obligation', results['oblig'])]:
        timeouts = sum(1 for _, status, _ in solver_results if status == 'timeout')
        errors = sum(1 for _, status, _ in solver_results if status == 'error')
        ok = sum(1 for _, status, _ in solver_results if status == 'ok')
        
        ok_times = [r[2] for r in solver_results if r[1] == 'ok']
        if ok_times:
            avg_time = sum(ok_times) / len(ok_times)
        else:
            avg_time = 0
        
        print(f"{solver_name}: {ok} OK, {timeouts} timeouts, {errors} errors")
        if ok > 0:
            print(f"  Average runtime (OK cases): {avg_time:.1f} ms")
    
    print("\n" + "=" * 60)
    print("Generating plot...")
    print("=" * 60)
    plot_results(results, timeout_sec)
    
    return 0

if __name__ == "__main__":
    exit(main())

