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
import signal
import tempfile
from pathlib import Path
import shutil
import traceback
import sys
from typing import Any


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

    # Determine pattern name and construct output filename early so a signal handler can write it
    base = os.path.splitext(os.path.basename(args.formula))[0]
    sjid = os.environ.get('SLURM_JOB_ID') or 'local'
    sat = os.environ.get('SLURM_ARRAY_TASK_ID') or str(args.run_idx)
    mode_tag = (args.mode or os.environ.get('MODE')) or 'nomode'
    out_fname = f"{base}.{mode_tag}.job{sjid}_task{sat}.run{args.run_idx}.json"
    out_file = os.path.join(args.out_dir, out_fname)

    # Build metadata early so signal handler can write a partial report
    metadata = {
        'pattern_file': args.formula,
        'partition_file': args.partition,
        'run_idx': args.run_idx,
        'mode': mode_tag,
        'hostname': socket.gethostname(),
        'slurm_job_id': os.environ.get('SLURM_JOB_ID'),
        'slurm_array_task_id': os.environ.get('SLURM_ARRAY_TASK_ID'),
        'timestamp': time.time(),
    }

    START_TIME = time.monotonic()

    def write_atomic(report_obj, path):
        def sanitize(o: Any):
            # Convert bytes to str, recursively sanitize containers, and
            # fall back to str() for other non-serializable objects.
            if isinstance(o, bytes):
                return o.decode('utf-8', errors='backslashreplace')
            if isinstance(o, str) or o is None or isinstance(o, (int, float, bool)):
                return o
            if isinstance(o, dict):
                return {str(k): sanitize(v) for k, v in o.items()}
            if isinstance(o, (list, tuple)):
                return [sanitize(v) for v in o]
            # Path-like objects or other objects -> convert to str
            try:
                return str(o)
            except Exception:
                return repr(o)

        Path(path).parent.mkdir(parents=True, exist_ok=True)
        tmp = str(path) + '.tmp'
        try:
            print(f"[job_runner] writing tmp file: {tmp}", file=sys.stderr, flush=True)
            with open(tmp, 'w') as f:
                json.dump(sanitize(report_obj), f, indent=2)
                f.flush()
                os.fsync(f.fileno())
            print(f"[job_runner] tmp written, attempting atomic replace -> {path}", file=sys.stderr, flush=True)
            try:
                os.replace(tmp, path)
                print(f"[job_runner] os.replace succeeded: {path}", file=sys.stderr, flush=True)
                return
            except Exception as e:
                print(f"[job_runner] os.replace failed: {e!r}", file=sys.stderr, flush=True)
                print(traceback.format_exc(), file=sys.stderr, flush=True)

            # Try a more robust fallback that works across filesystems.
            try:
                print(f"[job_runner] attempting shutil.move({tmp}, {path})", file=sys.stderr, flush=True)
                shutil.move(tmp, path)
                print(f"[job_runner] shutil.move succeeded: {path}", file=sys.stderr, flush=True)
                return
            except Exception as e:
                print(f"[job_runner] shutil.move failed: {e!r}", file=sys.stderr, flush=True)
                print(traceback.format_exc(), file=sys.stderr, flush=True)

            # As a last resort, write the file directly and remove the tmp file.
            try:
                print(f"[job_runner] writing final file directly: {path}", file=sys.stderr, flush=True)
                with open(path, 'w') as f:
                    json.dump(sanitize(report_obj), f, indent=2)
                    f.flush()
                    os.fsync(f.fileno())
                # remove tmp if it still exists
                if os.path.exists(tmp):
                    try:
                        os.remove(tmp)
                        print(f"[job_runner] removed tmp file: {tmp}", file=sys.stderr, flush=True)
                    except Exception as e:
                        print(f"[job_runner] failed to remove tmp file {tmp}: {e!r}", file=sys.stderr, flush=True)
                        print(traceback.format_exc(), file=sys.stderr, flush=True)
                return
            except Exception as e:
                print(f"[job_runner] final write failed: {e!r}", file=sys.stderr, flush=True)
                print(traceback.format_exc(), file=sys.stderr, flush=True)
                # Give up; leave tmp if we can't write the final file
                return
        except Exception as e:
            print(f"[job_runner] failed to create/write tmp file {tmp}: {e!r}", file=sys.stderr, flush=True)
            print(traceback.format_exc(), file=sys.stderr, flush=True)
            return

    def sigterm_handler(signum, frame):
        # Called when Slurm sends SIGTERM; write a partial report indicating termination
        elapsed = time.monotonic() - START_TIME
        partial = {
            **metadata,
            'cmd': None,
            'returncode': None,
            'timeout': True,
            'killed_by_signal': int(signum),
            'elapsed_s': elapsed,
            'max_rss_kb': None,
            'stdout': '',
            'stderr': f'Terminated by signal {signum} before run completion.'
        }
        try:
            write_atomic(partial, out_file)
            print(f'Wrote partial result due to signal {signum}: {out_file}')
        except Exception:
            pass
        # exit with code indicating signal
        os._exit(128 + int(signum))

    # Install signal handlers for graceful termination (best-effort)
    signal.signal(signal.SIGTERM, sigterm_handler)
    signal.signal(signal.SIGINT, sigterm_handler)

    # Write an initial 'started' JSON so that if the process is killed hard
    # (SIGKILL) before the final report is written, there is still a valid
    # JSON file present describing the run start. This is intentionally small
    # and will be replaced by the full report at the end of the run.
    try:
        started = {**metadata, 'status': 'started', 'partial': True}
        write_atomic(started, out_file)
    except Exception:
        # best-effort; continue even if we can't write the started file
        pass

    # Run
    result = run_once(args.binary, args.binary_args, args.singularity or None, args.formula, args.partition, args.timeout)

    # Augment result with metadata
    report = {**metadata, **result}

    # Truncate very large outputs to keep JSON sane
    MAX_LOG = 200_000
    if len(report.get('stdout') or '') > MAX_LOG:
        report['stdout_truncated'] = True
        report['stdout'] = (report.get('stdout') or '')[:MAX_LOG]
    if len(report.get('stderr') or '') > MAX_LOG:
        report['stderr_truncated'] = True
        report['stderr'] = (report.get('stderr') or '')[:MAX_LOG]

    # Write atomically
    write_atomic(report, out_file)

    print(f"Wrote result: {out_file}")
    # Exit status: 0 if run completed (even if binary returned non-zero), 2 if timed out
    if result['timeout']:
        sys.exit(2)
    else:
        # Return the child's returncode if available, otherwise 1
        sys.exit(result['returncode'] if result['returncode'] is not None else 1)


if __name__ == '__main__':
    main()
