#!/usr/bin/env python3
"""
Run a single benchmark run (intended to be invoked by a Slurm job).

This script executes the target binary (optionally inside a Singularity image),
enforces a timeout, measures elapsed time and peak memory (RSS) using resource
usage, captures stdout/stderr, and writes a JSON result file to the given
output directory.

It is designed to be robust inside Slurm jobs and to produce one JSON file per
run (no shared locking required).
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import time
import resource
import socket


def run_once(binary, binary_args, singularity, formula_file, partition_file, timeout_s, extra_env=None):
    # Build command list
    cmd = []
    if singularity:
        # Run using the `singularity` executable and pass the image path and binary
        cmd.extend(['singularity', 'exec', '--contain', '--no-home', singularity, binary])
    else:
        cmd.append(binary)

    # Add binary args (string -> split) and the mandatory -i -p if provided
    if binary_args:
        # allow passing as a string
        cmd.extend(shlex.split(binary_args))

    if formula_file:
        cmd.extend(['-i', formula_file])
    if partition_file:
        cmd.extend(['-p', partition_file])

    # Prepare environment
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)

    start = time.monotonic()
    try:
        # Run and capture output; enforce timeout
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
            env=env,
            text=True
        )
        elapsed = time.monotonic() - start
        returncode = proc.returncode
        stdout = proc.stdout
        stderr = proc.stderr
        timed_out = False
    except subprocess.TimeoutExpired as e:
        elapsed = time.monotonic() - start
        # subprocess.TimeoutExpired does not capture partial output reliably for complex children; mark timeout
        returncode = None
        stdout = getattr(e, 'output', '') or ''
        stderr = getattr(e, 'stderr', '') or ''
        timed_out = True

    # Get resource usage for child processes
    try:
        r = resource.getrusage(resource.RUSAGE_CHILDREN)
        # ru_maxrss is in kilobytes on Linux
        max_rss_kb = getattr(r, 'ru_maxrss', 0)
    except Exception:
        max_rss_kb = 0

    return {
        'cmd': cmd,
        'returncode': returncode,
        'timeout': timed_out,
        'elapsed_s': elapsed,
        'max_rss_kb': max_rss_kb,
        'stdout': stdout,
        'stderr': stderr,
    }


def main():
    parser = argparse.ArgumentParser(description='Run a single benchmark job (for Slurm array)')
    parser.add_argument('--binary', required=True, help='Path to LydiaSyftEL binary inside container or host')
    parser.add_argument('--binary-args', default='', help='Extra arguments to pass to the binary (quoted)')
    parser.add_argument('--singularity', default='', help='Path to singularity image (optional)')
    parser.add_argument('--mode', default=None, help='Solver mode (el, cl, pm, wg, cb)')
    parser.add_argument('--formula', required=True, help='Path to .ltlfplus file')
    parser.add_argument('--partition', required=True, help='Path to .part file')
    parser.add_argument('--run-idx', type=int, required=True, help='Run index (1..N)')
    parser.add_argument('--out-dir', required=True, help='Directory where per-run JSON result will be written')
    parser.add_argument('--timeout', type=int, default=60, help='Timeout in seconds for this run')

    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    # Determine pattern name from formula file
    pattern = os.path.basename(args.formula)
    # Determine mode: prefer CLI arg, then env var
    mode = args.mode or os.environ.get('MODE')

    # Run
    result = run_once(args.binary, args.binary_args, args.singularity or None, args.formula, args.partition, args.timeout)

    # Augment result with metadata
    metadata = {
        'pattern_file': args.formula,
        'partition_file': args.partition,
        'run_idx': args.run_idx,
        'mode': mode,
        'hostname': socket.gethostname(),
        'slurm_job_id': os.environ.get('SLURM_JOB_ID'),
        'slurm_array_task_id': os.environ.get('SLURM_ARRAY_TASK_ID'),
        'timestamp': time.time(),
    }

    report = {**metadata, **result}

    # Truncate very large outputs to keep JSON sane
    MAX_LOG = 200_000
    if len(report['stdout']) > MAX_LOG:
        report['stdout_truncated'] = True
        report['stdout'] = report['stdout'][:MAX_LOG]
    if len(report['stderr']) > MAX_LOG:
        report['stderr_truncated'] = True
        report['stderr'] = report['stderr'][:MAX_LOG]

    # Write file
    # Construct a readable, unique filename including mode and Slurm ids when available
    base = os.path.splitext(os.path.basename(args.formula))[0]
    sjid = metadata.get('slurm_job_id') or 'local'
    sat = metadata.get('slurm_array_task_id') or str(args.run_idx)
    mode_tag = mode or 'nomode'
    out_fname = f"{base}.{mode_tag}.job{sjid}_task{sat}.run{args.run_idx}.json"
    out_file = os.path.join(args.out_dir, out_fname)
    with open(out_file + '.tmp', 'w') as f:
        json.dump(report, f, indent=2)
    os.replace(out_file + '.tmp', out_file)

    print(f"Wrote result: {out_file}")
    # Exit status: 0 if run completed (even if binary returned non-zero), 2 if timed out
    if result['timeout']:
        sys.exit(2)
    else:
        # Return the child's returncode if available, otherwise 1
        sys.exit(result['returncode'] if result['returncode'] is not None else 1)


if __name__ == '__main__':
    main()
