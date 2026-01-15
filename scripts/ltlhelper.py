#!/usr/bin/env python3
"""
ltlhelper.py - small helper to run LydiaSyftEL with different solver modes.

Usage: import this module or run from command line. It locates the local build binary
by default (../build/bin/LydiaSyftEL) and exposes convenience commands for:
 - EL (Emerson-Lei)
 - Buchi (classic, piterman)
 - CoBuchi
 - Manna-Pnueli (with/without adv)

The synthesizers mapping contains functions that build the proper command string.
"""

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    from scipy import stats
except Exception:
    stats = None

# locate project root and binary
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
DEFAULT_BINARY = os.path.join(PROJECT_ROOT, 'build', 'bin', 'LydiaSyftEL')

if not os.path.exists(DEFAULT_BINARY):
    # Try alternative common names
    alt = os.path.join(PROJECT_ROOT, 'build', 'LydiaSyftEL')
    if os.path.exists(alt):
        DEFAULT_BINARY = alt


def _ensure_binary(binary_path=None):
    """Return a usable binary path or raise FileNotFoundError."""
    if binary_path:
        if os.path.exists(binary_path) and os.access(binary_path, os.X_OK):
            return binary_path
        raise FileNotFoundError(f"Binary not found or not executable: {binary_path}")

    if os.path.exists(DEFAULT_BINARY) and os.access(DEFAULT_BINARY, os.X_OK):
        return DEFAULT_BINARY

    # Try PATH
    which = shutil.which('LydiaSyftEL')
    if which:
        return which

    raise FileNotFoundError("Could not find LydiaSyftEL binary. Please build the project or provide the path.")


# Command builders for the various solver configurations. Each returns a full command string.
# Parameters: input_file, part_file, env_starts (bool), binary (optional), extra kwargs

def cmd_el(input_file, part_file, env_starts=False, binary=None, **kwargs):
    binp = _ensure_binary(binary)
    s = 1 if env_starts else 0
    # EL uses -g 0; obligation simplification usually not applicable (use 0)
    return f"{binp} -i {input_file} -p {part_file} -s {s} -g 0 --obligation-simplification 0"


def cmd_buchi(input_file, part_file, env_starts=False, binary=None, mode='cl', obligation_simplification=1, **kwargs):
    """mode: 'cl' (classic), 'pm' (piterman), 'cb' (cobuchi)"""
    binp = _ensure_binary(binary)
    s = 1 if env_starts else 0
    return f"{binp} -i {input_file} -p {part_file} -s {s} -g 1 --obligation-simplification {int(bool(obligation_simplification))} -b {mode}"


def cmd_mannapnueli(input_file, part_file, env_starts=False, binary=None, adv=False, obligation_simplification=1, **kwargs):
    binp = _ensure_binary(binary)
    s = 1 if env_starts else 0
    mode = 2 if adv else 1
    return f"{binp} -i {input_file} -p {part_file} -s {s} -g {mode} --obligation-simplification {int(bool(obligation_simplification))}"
    
def cmd_ltlfsynth(input_file, part_file, env_starts=False, binary=None, **kwargs):
    """Builder for an external LTLf synthesizer (e.g., Syftmax). If binary is not provided,
    try to find 'Syftmax' on PATH or use common fallback path.
    This synthesizer expects .ltlf extension for formula files.
    """
    if binary:
        binp = binary
    else:
        # try PATH
        binp = shutil.which('Syftmax')
        if not binp:
            # fallback common location
            candidate = os.path.join(os.path.expanduser('~'), 'SyftNew', 'build', 'bin', 'Syftmax')
            if os.path.exists(candidate):
                binp = candidate
            else:
                raise FileNotFoundError('Could not find Syftmax binary; please provide --binary')

    # many LTLf tools use different flags; assume Syftmax style: -f <formula> -p <part>
    return f"{binp} -f {input_file} -p {part_file}"


synthesizers = {
    'EL': {
        'description': 'Emerson-Lei solver (Zielonka/EL)',
        'builder': cmd_el,
        'default_kwargs': {}
    },
    'Buchi-Classic': {
        'description': 'Optimised Buchi (classic)',
        'builder': lambda ip, p, env_starts, binary=None, **kw: cmd_buchi(ip, p, env_starts, binary=binary, mode='cl', **kw),
        'default_kwargs': {'mode': 'cl', 'obligation_simplification': 1}
    },
    'Buchi-Piterman': {
        'description': 'Optimised Buchi (Piterman mode)',
        'builder': lambda ip, p, env_starts, binary=None, **kw: cmd_buchi(ip, p, env_starts, binary=binary, mode='pm', **kw),
        'default_kwargs': {'mode': 'pm', 'obligation_simplification': 1}
    },
    'CoBuchi': {
        'description': 'Optimised CoBuchi (as a Buchi-mode alternative)',
        'builder': lambda ip, p, env_starts, binary=None, **kw: cmd_buchi(ip, p, env_starts, binary=binary, mode='cb', **kw),
        'default_kwargs': {'mode': 'cb', 'obligation_simplification': 1}
    },
    'MannaPnueli': {
        'description': 'Manna-Pnueli (non-adv)',
        'builder': lambda ip, p, env_starts, binary=None, **kw: cmd_mannapnueli(ip, p, env_starts, binary=binary, adv=False, **kw),
        'default_kwargs': {'adv': False}
    },
    'MannaPnueli-Adv': {
        'description': 'Manna-Pnueli (adversarial)',
        'builder': lambda ip, p, env_starts, binary=None, **kw: cmd_mannapnueli(ip, p, env_starts, binary=binary, adv=True, **kw),
        'default_kwargs': {'adv': True}
    },
 }

# Add LTLf synthesizer (external tool like Syftmax)
synthesizers['LTLf'] = {
    'description': 'External LTLf synthesizer (Syftmax-like)',
    'builder': cmd_ltlfsynth,
    'default_kwargs': {}
}



def run_synthesizer(synth_name, input_file, part_file=None, env_starts=False, binary=None, dry_run=False, **kwargs):
    """Run a synthesizer by name.

    Args:
        synth_name: key from synthesizers dict
        input_file: full path to formula file
        part_file: full path to partition (.part) file; if None, will try input_file with .part extension
        env_starts: whether environment starts (True) or agent (False)
        binary: optional path to LydiaSyftEL binary
        dry_run: if True, print the command but do not execute
        kwargs: passed to builder (e.g., mode, obligation_simplification)
    """
    if synth_name not in synthesizers:
        raise KeyError(f"Unknown synthesizer: {synth_name}")

    if part_file is None:
        part_file = os.path.splitext(input_file)[0] + '.part'

    builder = synthesizers[synth_name]['builder']
    # If input_file supplied without extension, try to resolve based on synthesizer type
    base, ext = os.path.splitext(input_file)
    if ext == '':
        # choose extension based on synth type: LTLf+ uses .ltlfplus, LTLf uses .ltlf
        if synth_name == 'EL' or synth_name.startswith('Buchi') or synth_name == 'CoBuchi' or synth_name.startswith('Manna'):
            input_file = base + '.ltlfplus'
        else:
            input_file = base + '.ltlf'

    if part_file is None:
        part_file = os.path.splitext(input_file)[0] + '.part'

    cmd = builder(input_file, part_file, env_starts, binary=binary, **kwargs)

    print(f"Running {synth_name}: {synthesizers[synth_name]['description']}")
    print(f"Command: {cmd}")

    if dry_run:
        return cmd

    # Run the command and stream output to console
    try:
        res = subprocess.run(cmd, shell=True)
        return res.returncode
    except Exception as e:
        print(f"Error running command: {e}")
        return -1


def run_and_measure(cmd, runs=1):
    """Run command multiple times and return timings (ms) and stats.

    Returns dict: {'runs': [ms,...], 'mean':..., 'stddev':..., 'ci95': half-width}
    """
    if runs <= 0:
        return {'runs': [], 'mean': 0.0, 'stddev': 0.0, 'ci95': 0.0, 'success_count': 0}

    timings = []
    success_count = 0
    errors = 0
    outputs = []
    for i in range(runs):
        # run but capture output (do not print solver output)
        start = time.perf_counter()
        try:
            res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            elapsed = (time.perf_counter() - start) * 1000.0
            out = (res.stdout or "") + "\n" + (res.stderr or "")
            outputs.append(out)
            if res.returncode == 0:
                timings.append(elapsed)
                success_count += 1
            else:
                errors += 1
        except Exception as e:
            errors += 1
            outputs.append(str(e))

    if success_count == 0:
        return {'runs': timings, 'mean': 0.0, 'stddev': 0.0, 'ci95': 0.0, 'success_count': success_count, 'errors': errors, 'outputs': outputs}

    import math
    mean = sum(timings) / len(timings)
    if len(timings) > 1:
        # sample stddev
        variance = sum((x - mean) ** 2 for x in timings) / (len(timings) - 1)
        stddev = math.sqrt(variance)
        if stats is None:
            raise RuntimeError("scipy is required to compute 95% confidence intervals; please install scipy")
        sem = stddev / math.sqrt(len(timings))
        tval = stats.t.ppf(1.0 - 0.025, df=(len(timings) - 1))
        ci95 = tval * sem
    else:
        stddev = 0.0
        ci95 = 0.0

    return {'runs': timings, 'mean': mean, 'stddev': stddev, 'ci95': ci95, 'success_count': success_count, 'errors': errors, 'outputs': outputs}


if __name__ == '__main__':
    import argparse
    import glob
    import json
    import re

    parser = argparse.ArgumentParser(description='Helper to run LydiaSyftEL or external LTLf synthesizers')
    parser.add_argument('synth', help='Synthesizer key (e.g. EL, Buchi-Classic, Buchi-Piterman, CoBuchi, LTLf, MannaPnueli)')
    parser.add_argument('input', help='Input file or glob pattern (you can use * to run many, e.g. examples/pattern_*)')
    parser.add_argument('-p', '--part', help='Partition file (optional)')
    parser.add_argument('--env-starts', action='store_true', help='Environment starts this game')
    parser.add_argument('--binary', help='Path to synthesizer binary (overrides detection)')
    parser.add_argument('-r', '--runs', type=int, default=1, help='Number of runs to average (default 1)')
    parser.add_argument('--dry-run', action='store_true', help='Print command(s) but do not execute')
    parser.add_argument('--timeout', type=int, default=0, help='Per-run timeout seconds (0 = no timeout)')
    parser.add_argument('--json', dest='json_out', help='Write JSON summary to this file')

    args = parser.parse_args()

    synth = args.synth
    input_arg = args.input
    part_arg = args.part
    env = args.env_starts
    binp = args.binary
    runs = max(1, args.runs)
    dry = args.dry_run
    timeout_sec = args.timeout
    json_out = args.json_out

    if synth not in synthesizers:
        print(f"Unknown synthesizer '{synth}'. Available: {', '.join(sorted(synthesizers.keys()))}")
        sys.exit(1)

    # Expand input pattern. If input contains a '*' or other glob chars, expand.
    inputs = []
    if any(ch in input_arg for ch in ['*', '?', '[']):
        # If no extension in pattern, try .ltlfplus (preferred) and .ltlf
        base_name = os.path.basename(input_arg)
        if '.' not in base_name:
            pattern_plus = input_arg + '.ltlfplus'
            pattern_ltf = input_arg + '.ltlf'
            matches = glob.glob(pattern_plus)
            if not matches:
                matches = glob.glob(pattern_ltf)
        else:
            matches = glob.glob(input_arg)
        # sort numerically by first number found in filename
        def extract_num(p):
            m = re.search(r'(\d+)', os.path.basename(p))
            return int(m.group(1)) if m else 0
        inputs = sorted(matches, key=extract_num)
    else:
        inputs = [input_arg]

    if not inputs:
        print(f"No input files matched the pattern: {input_arg}")
        sys.exit(2)

    summary = []

    for infile in inputs:
        # if infile has no extension, let run_synthesizer resolve it
        if dry:
            cmd = run_synthesizer(synth, infile, part_file=part_arg, env_starts=env, binary=binp, dry_run=True)
            print(cmd)
            continue

        # build command string
        cmd = run_synthesizer(synth, infile, part_file=part_arg, env_starts=env, binary=binp, dry_run=True)

        # run multiple times, capture outputs
        results = []
        all_outputs = []
        for rep in range(runs):
            try:
                if timeout_sec and timeout_sec > 0:
                    proc = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout_sec)
                else:
                    proc = subprocess.run(cmd, shell=True, capture_output=True, text=True)
                elapsed = None
                # We did not measure elapsed precisely here; use run_and_measure for timing
                # But we still need realizability detection per run
                out = (proc.stdout or '') + '\n' + (proc.stderr or '')
                all_outputs.append(out)
                # detect REALIZABLE / UNREALIZABLE (check UNREL first)
                status = 'UNKNOWN'
                if 'Unrealizable' in out:
                    status = 'UNREALIZABLE'
                elif 'Realizable' in out:
                    status = 'REALIZABLE'
                results.append({'returncode': proc.returncode, 'status': status, 'output': out})
            except subprocess.TimeoutExpired as e:
                results.append({'returncode': None, 'status': 'TIMEOUT', 'output': ''})

        # For timing, call run_and_measure separately (it re-runs). The user requested timing and 95% CI using scipy.
        timing_stats = run_and_measure(cmd, runs=runs)

        # Consolidate status: if any run reported UNREALIZABLE -> UNREALIZABLE (user wanted check Unrel first), else if any REALIZABLE -> REALIZABLE
        statuses = [r['status'] for r in results]
        final_status = 'UNKNOWN'
        if any(s == 'UNREALIZABLE' for s in statuses):
            final_status = 'UNREALIZABLE'
        elif any(s == 'REALIZABLE' for s in statuses):
            final_status = 'REALIZABLE'

        entry = {
            'input': infile,
            'status': final_status,
            'timing': timing_stats,
            'per_run_statuses': statuses,
            'raw_outputs': timing_stats.get('outputs', all_outputs)[:5]  # keep first few outputs for debugging
        }
        summary.append(entry)

    # Print compact summary
    for e in summary:
        print(f"{os.path.basename(e['input'])}: {e['status']} | mean={e['timing']['mean']:.1f} ms ci95={e['timing']['ci95']:.1f} ms (n={e['timing']['success_count']})")

    # Write JSON if requested
    if json_out:
        with open(json_out, 'w') as jf:
            json.dump({'runs': summary}, jf, indent=2)
        print(f"Wrote JSON summary to {json_out}")

    sys.exit(0)

