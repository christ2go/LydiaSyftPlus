#!/usr/bin/env python3
"""
Submit benchmark jobs to Slurm for all pattern examples in a directory.

This script will:
 - discover files like pattern_*.ltlfplus in the given examples directory
 - for each pattern create a Slurm array job with --array=1-N (N = runs)
 - each array task calls the bundled `job_runner.py` which writes a per-run JSON
 - optionally wait for all results and aggregate them using the aggregator

Notes:
 - The submitter writes job scripts under OUT_DIR/jobs and logs under OUT_DIR/logs
 - Per-run JSON files are written to OUT_DIR/results
 - This approach avoids concurrent writes to the same file: each run produces its own JSON
"""
import argparse
import os
import subprocess
import sys
import time
import json
import shutil
from pathlib import Path

from datetime import datetime


TEMPLATE = Path(__file__).parent / 'job_slurm.sh.template'


def find_examples(examples_dir, prefix='pattern'):
    p = Path(examples_dir)
    if not p.exists():
        raise FileNotFoundError(f"Examples directory not found: {examples_dir}")
    files = sorted([f for f in p.iterdir() if f.is_file() and f.name.startswith(prefix + '_') and f.name.endswith('.ltlfplus')],
                   key=lambda x: int(x.name.replace(prefix + '_', '').replace('.ltlfplus', '')))
    return files


def write_job_script(out_dir, pattern_file, partition_file, binary, singularity, binary_args, timeout, runs, sbatch_opts, mode, single_node=False):
    # Create a job script under out_dir/jobs
    job_name = pattern_file.stem
    jobs_dir = out_dir / 'jobs'
    jobs_dir.mkdir(parents=True, exist_ok=True)
    logs_dir = out_dir / 'logs'
    logs_dir.mkdir(parents=True, exist_ok=True)

    # Copy the job runner into jobs_dir so compute nodes can invoke it via an absolute path
    src_runner = Path(__file__).parent / 'job_runner.py'
    dst_runner = jobs_dir / 'job_runner.py'
    try:
        shutil.copy2(src_runner, dst_runner)
        os.chmod(dst_runner, 0o755)
    except Exception:
        # If copy fails, we'll fallback to template's local lookup logic
        dst_runner = None

    # include mode and obligation solver in job name to distinguish jobs
    job_script = jobs_dir / f"job_{job_name}_{mode}.sh"
    with open(TEMPLATE, 'r') as t, open(job_script, 'w') as out:
        # Write SBATCH header
        out.write('#!/usr/bin/env bash\n')
        sjob = f"{job_name}_{mode}"
        out.write(f"#SBATCH --job-name={sjob}\n")
        out.write(f"#SBATCH --output={logs_dir}/{sjob}-%A_%a.out\n")
        # append other sbatch opts
        for k, v in sbatch_opts.items():
            out.write(f"#SBATCH {k}={v}\n")
        # array directive (skip for single-node sequential mode)
        if not single_node:
            out.write(f"#SBATCH --array=1-{runs}\n")
        else:
            # request a single node/task for sequential runs
            out.write(f"#SBATCH --nodes=1\n")
        out.write('\n')

        # Export environment variables that template expects
        # If running inside a Singularity image that already contains examples at /opt/examples,
        # rewrite the paths so they point to the image's copy (avoids host path not found inside image).
        if singularity:
            # compute path relative to the 'examples' folder so we can point to /opt/examples/<rel>
            parts = pattern_file.parts
            if 'examples' in parts:
                idx = parts.index('examples')
                rel = Path(*parts[idx+1:])
                container_formula = Path('/opt/examples') / rel
            else:
                container_formula = pattern_file

            parts_p = partition_file.parts
            if 'examples' in parts_p:
                idxp = parts_p.index('examples')
                relp = Path(*parts_p[idxp+1:])
                container_partition = Path('/opt/examples') / relp
            else:
                container_partition = partition_file

            out.write(f"FORMULA_FILE=\"{container_formula}\"\n")
            out.write(f"PARTITION_FILE=\"{container_partition}\"\n")
        else:
            out.write(f"FORMULA_FILE=\"{pattern_file}\"\n")
            out.write(f"PARTITION_FILE=\"{partition_file}\"\n")

        out.write(f"BINARY=\"{binary}\"\n")
        out.write(f"SINGULARITY=\"{singularity or ''}\"\n")
        # Build binary args: always start with starting player -s 0
        # Mode-specific args
        mode_args = ''
        if mode == 'el':
            # explicit obligation-simplification=0 for the EL (explicit) mode
            mode_args = '-s 0 -g 0 --obligation-simplification 0'
        else:
            # optimized modes use -g 1, obligation simplification on, and -b <code>
            mode_args = f'-s 0 -g 1 --obligation-simplification 1 -b {mode}'

        # If caller provided extra binary_args, append them
        combined_args = (binary_args or '').strip()
        if combined_args:
            combined = f"{mode_args} {combined_args}"
        else:
            combined = mode_args

        out.write(f"BINARY_ARGS=\"{combined}\"\n")
        # Export MODE so the job_runner can pick it up and include it in the result filename
        out.write(f"export MODE=\"{mode}\"\n")
        out.write(f"OUT_DIR=\"{out_dir / 'results'}\"\n")
        out.write(f"TIMEOUT=\"{timeout}\"\n")   
        if not single_node:
            out.write(f"RUN_IDX=\"${{SLURM_ARRAY_TASK_ID}}\"\n")

        # Inject JOB_RUNNER absolute path (so template doesn't rely on $0-based lookup)
        if dst_runner is not None:
            out.write(f"JOB_RUNNER=\"{dst_runner}\"\n")
        out.write('\n')

        # Append the body of the template (skip its shebang)
        with open(TEMPLATE, 'r') as tmpl:
            tmpl_text = tmpl.read()
        # write everything after the first line
        tmpl_body = '\n'.join(tmpl_text.splitlines()[1:])
        if single_node:
            # Wrap the template body in a loop that sets RUN_IDX for each sequential run
            # with early-exit logic: skip remaining runs if one fails
            out.write('skip_remaining=0\n')
            out.write(f"for RUN_IDX in $(seq 1 {runs}); do\n")
            out.write('  if [ "$skip_remaining" -eq 1 ]; then\n')
            out.write('    echo "Skipping run ${RUN_IDX} (previous run failed)"\n')
            out.write('    continue\n')
            out.write('  fi\n')
            # indent the template body
            for line in tmpl_body.splitlines():
                out.write('  ' + line + '\n')
            out.write('  exit_code=$?\n')
            out.write('  if [ "$exit_code" -ne 0 ]; then\n')
            out.write('    echo "Run ${RUN_IDX} failed with exit code ${exit_code}, skipping remaining runs"\n')
            out.write('    skip_remaining=1\n')
            out.write('  fi\n')
            out.write('done\n')
        else:
            out.write(tmpl_body)

    os.chmod(job_script, 0o755)
    return job_script


TRANSIENT_SBATCH_ERRORS = (
    'socket timed out',
    'connection timed out',
    'communications connection failure',
)


def submit_script(script_path, retries=3, retry_delay=5.0, retry_backoff=2.0):
    # Submit with sbatch and return job id (parsable form)
    max_attempts = max(1, retries)
    delay = retry_delay
    for attempt in range(1, max_attempts + 1):
        try:
            res = subprocess.run(['sbatch', '--parsable', str(script_path)], capture_output=True, text=True, check=True)
            jobid = res.stdout.strip()
            print(f"Submitted {script_path.name} -> job {jobid}")
            return jobid
        except subprocess.CalledProcessError as e:
            stderr = (e.stderr or '').strip()
            stdout = (e.stdout or '').strip()
            combined = '\n'.join(filter(None, [stderr, stdout])).lower()
            is_transient = any(pattern in combined for pattern in TRANSIENT_SBATCH_ERRORS)
            if is_transient and attempt < max_attempts:
                print(f"sbatch transient failure (attempt {attempt}/{max_attempts}): {stderr or stdout}. Retrying in {delay:.1f}s...",
                      file=sys.stderr)
                time.sleep(delay)
                delay *= retry_backoff if retry_backoff > 0 else 1
                continue
            print('sbatch failed:', stderr or stdout, file=sys.stderr)
            raise


def wait_for_results(out_dir, expected_count, timeout_s=3600, poll_interval=10):
    results_dir = out_dir / 'results'
    results_dir.mkdir(parents=True, exist_ok=True)
    start = time.time()
    while True:
        files = list(results_dir.glob('*.json'))
        if len(files) >= expected_count:
            return True
        if time.time() - start > timeout_s:
            return False
        time.sleep(poll_interval)


def aggregate(out_dir, aggregated_file):
    # Read all per-run JSONs and group by pattern file
    results_dir = out_dir / 'results'
    entries = list(results_dir.glob('*.json'))
    data = {}
    for p in entries:
        try:
            with open(p, 'r') as f:
                r = json.load(f)
        except Exception:
            continue
        key = os.path.basename(r.get('pattern_file') or p.name)
        data.setdefault(key, []).append(r)
    with open(aggregated_file, 'w') as f:
        json.dump({'generated_at': time.time(), 'results': data}, f, indent=2)
    print(f"Aggregated {len(entries)} run files -> {aggregated_file}")


def main():
    parser = argparse.ArgumentParser(description='Submit benchmark jobs to Slurm (pattern directory)')
    parser.add_argument('--examples-dir', help='Directory with examples (default: examples/frompaper)', default=None)
    parser.add_argument('--pattern', default='pattern', help='Pattern prefix')
    parser.add_argument('--runs', type=int, default=3, help='Number of runs per pattern')
    parser.add_argument('--timeout', type=int, default=60, help='Timeout per run (s)')
    parser.add_argument('--binary', default='LydiaSyftEL', help='Path to binary (inside singularity or host)')
    parser.add_argument('--singularity', default='', help='Path to singularity image (optional)')
    parser.add_argument('--binary-args', default='', help='Extra arguments to binary (quoted)')
    parser.add_argument('--out-dir', default='bench_out', help='Directory for jobs/logs/results')
    parser.add_argument('--wait', action='store_true', help='Wait for all runs to finish (polling)')
    parser.add_argument('--aggregate', action='store_true', help='Aggregate per-run JSONs into one file after completion')
    parser.add_argument('--sbatch-time', default='00:10:00', help='Slurm time limit (e.g. 00:10:00)')
    parser.add_argument('--sbatch-mem', default='16G', help='Slurm memory per task')
    parser.add_argument('--sbatch-cpus', default='1', help='Slurm cpus-per-task')
    parser.add_argument('--sbatch-partition', default='', help='Slurm partition (optional)')
    parser.add_argument('--sbatch-retries', type=int, default=3,
                        help='Retry sbatch submission up to N times on transient socket errors (default: 3)')
    parser.add_argument('--sbatch-retry-delay', type=float, default=5.0,
                        help='Initial delay in seconds before retrying sbatch submission (default: 5.0)')
    parser.add_argument('--sbatch-retry-backoff', type=float, default=2.0,
                        help='Multiplicative backoff applied to the retry delay (default: 2.0)')
    parser.add_argument('--single-node', action='store_true', help='Run all runs for a pattern sequentially in a single Slurm job (no array)')
    parser.add_argument('--single-job', action='store_true', help='Run all patterns and modes sequentially in one Slurm job on a single node')
    parser.add_argument('--max-examples', type=int, default=None, help='Limit to first N examples')
    parser.add_argument('--modes', type=str, default='el,cl,pm,wg,cb',
                        help='Comma-separated solver modes to run: el,cl,pm,wg,cb (default: all)')
    args = parser.parse_args()

    # Determine examples dir
    if args.examples_dir:
        examples_dir = Path(args.examples_dir)
    else:
        script_dir = Path(__file__).resolve().parent
        project_root = script_dir.parent.parent
        examples_dir = project_root / 'examples' / 'frompaper'

    examples = find_examples(examples_dir, prefix=args.pattern)
    if args.max_examples:
        examples = examples[:args.max_examples]

    if not examples:
        print('No example patterns found. Exiting.')
        sys.exit(1)

    out_dir = Path(args.out_dir).resolve()
    # Create a timestamped run subdirectory so separate invocations don't overwrite each other.
    # Format: YYYY-MM-DD-HHMM-<pattern>
    # include seconds to make the run directory unique even for rapid successive runs
    timestamp = datetime.now().strftime('%Y-%m-%d-%H%M%S')
    run_subdir_name = f"{timestamp}-{args.pattern}"
    run_out_dir = out_dir / run_subdir_name
    (run_out_dir / 'results').mkdir(parents=True, exist_ok=True)
    (run_out_dir / 'logs').mkdir(parents=True, exist_ok=True)
    (run_out_dir / 'jobs').mkdir(parents=True, exist_ok=True)

    sbatch_opts = {
        '--time': args.sbatch_time,
        '--mem': args.sbatch_mem,
        '--cpus-per-task': args.sbatch_cpus,
    }
    if args.sbatch_partition:
        sbatch_opts['--partition'] = args.sbatch_partition

    # If user left --sbatch-time as the default, auto-adjust it to be a bit
    # higher than per-run timeout so the runner can clean up and write results.
    # Use a small margin (10 seconds) as a default.
    default_sbatch_time = '00:10:00'
    try:
        user_provided_time = args.sbatch_time != default_sbatch_time
    except Exception:
        user_provided_time = True
    if not user_provided_time:
        # Add a fixed 1 minute margin to the per-run timeout so Slurm gives
        # the runner a small window to cleanup and write results.
        try:
            per_run = int(args.timeout)
        except Exception:
            per_run = 0
        margin = 60
        total_seconds = int(args.timeout) + margin
        hh = total_seconds // 3600
        mm = (total_seconds % 3600) // 60
        ss = total_seconds % 60
        sbatch_opts['--time'] = f"{hh:02d}:{mm:02d}:{ss:02d}"

    modes = [m.strip() for m in args.modes.split(',') if m.strip()]

    submitted = []
    expected_runs = 0

    # If the user requested a single job for all patterns/modes, create one job script
    # that runs everything sequentially on a single node and submit it once.
    if args.single_job:
        # For single-job mode, we need a much longer time limit since we're running
        # all patterns × modes × runs sequentially. Auto-calculate if user didn't specify.
        if not user_provided_time:
            # Estimate: (number of patterns) × (number of modes) × (runs) × (timeout per run) + margin
            # This is a rough upper bound; early-exit will help if runs fail/timeout
            estimated_seconds = len(examples) * len(modes) * args.runs * args.timeout
            # Add 10% margin for overhead
            estimated_seconds = int(estimated_seconds * 1.1)
            # Cap at 24 hours (86400 seconds) as a safety limit
            total_seconds = min(estimated_seconds, 86400)
            hh = total_seconds // 3600
            mm = (total_seconds % 3600) // 60
            ss = total_seconds % 60
            sbatch_opts['--time'] = f"{hh:02d}:{mm:02d}:{ss:02d}"
            print(f"Auto-calculated time limit for single-job: {sbatch_opts['--time']} ({len(examples)} patterns × {len(modes)} modes × {args.runs} runs × {args.timeout}s)")
        
        def write_global_job_script(out_dir, patterns, modes, binary, singularity, binary_args, timeout, runs, sbatch_opts):
            jobs_dir = out_dir / 'jobs'
            jobs_dir.mkdir(parents=True, exist_ok=True)
            logs_dir = out_dir / 'logs'
            logs_dir.mkdir(parents=True, exist_ok=True)

            # Copy runner
            src_runner = Path(__file__).parent / 'job_runner.py'
            dst_runner = jobs_dir / 'job_runner.py'
            try:
                shutil.copy2(src_runner, dst_runner)
                os.chmod(dst_runner, 0o755)
            except Exception:
                dst_runner = None

            job_script = jobs_dir / f"job_all_patterns.sh"
            with open(TEMPLATE, 'r') as t, open(job_script, 'w') as out:
                out.write('#!/usr/bin/env bash\n')
                sjob = 'bench_all'
                out.write(f"#SBATCH --job-name={sjob}\n")
                out.write(f"#SBATCH --output={logs_dir}/{sjob}-%A.out\n")
                for k, v in sbatch_opts.items():
                    out.write(f"#SBATCH {k}={v}\n")
                # single node for the whole job
                out.write(f"#SBATCH --nodes=1\n")
                out.write('\n')

                out.write(f"BINARY=\"{binary}\"\n")
                out.write(f"SINGULARITY=\"{singularity or ''}\"\n")
                out.write(f"BINARY_ARGS=\"{(binary_args or '').strip()}\"\n")
                out.write(f"OUT_DIR=\"{out_dir / 'results'}\"\n")
                out.write(f"TIMEOUT=\"{timeout}\"\n")
                if dst_runner is not None:
                    out.write(f"JOB_RUNNER=\"{dst_runner}\"\n")
                out.write('\n')

                # Prepare arrays of formulas and partitions
                # If using Singularity, rewrite paths to point to /opt/examples inside container
                out.write('FORMULAS=(\n')
                for p in patterns:
                    if singularity:
                        # Rewrite path for Singularity container
                        parts = Path(p).parts
                        if 'examples' in parts:
                            idx = parts.index('examples')
                            rel = Path(*parts[idx+1:])
                            container_path = Path('/opt/examples') / rel
                        else:
                            container_path = Path(p)
                        out.write(f"  \"{container_path}\"\n")
                    else:
                        out.write(f"  \"{p}\"\n")
                out.write(')\n')
                out.write('PARTS=(\n')
                for p in patterns:
                    part_path = Path(p).with_suffix('.part')
                    if singularity:
                        # Rewrite path for Singularity container
                        parts = part_path.parts
                        if 'examples' in parts:
                            idx = parts.index('examples')
                            rel = Path(*parts[idx+1:])
                            container_path = Path('/opt/examples') / rel
                        else:
                            container_path = part_path
                        out.write(f"  \"{container_path}\"\n")
                    else:
                        out.write(f"  \"{part_path}\"\n")
                out.write(')\n')
                out.write('\n')

                # modes list
                out.write('MODES=(\n')
                for m in modes:
                    out.write(f"  \"{m}\"\n")
                out.write(')\n')
                out.write('\n')

                # Loop over patterns, modes, and runs
                # Early-exit logic: track failed modes globally and skip them for all remaining patterns
                out.write('# Initialize array to track which modes have failed\n')
                out.write('declare -A skip_mode\n')
                out.write('for mode in "${MODES[@]}"; do\n')
                out.write('  skip_mode[$mode]=0\n')
                out.write('done\n')
                out.write('\n')
                out.write('for i in "${!FORMULAS[@]}"; do\n')
                out.write('  formula="${FORMULAS[$i]}"\n')
                out.write('  partition="${PARTS[$i]}"\n')
                out.write('  for mode in "${MODES[@]}"; do\n')
                out.write('    # Check if this mode has already failed on a previous pattern\n')
                out.write('    if [ "${skip_mode[$mode]}" -eq 1 ]; then\n')
                out.write('      echo "Skipping formula=${formula}, mode=${mode} (mode failed on earlier pattern)"\n')
                out.write('      continue\n')
                out.write('    fi\n')
                out.write('    # Set mode-specific binary arguments\n')
                out.write('    if [ "$mode" = "el" ]; then\n')
                out.write('      MODE_ARGS="-s 0 -g 0 --obligation-simplification 0"\n')
                out.write('    else\n')
                out.write('      MODE_ARGS="-s 0 -g 1 --obligation-simplification 1 -b ${mode}"\n')
                out.write('    fi\n')
                out.write('    # Combine with user-provided binary args\n')
                out.write('    if [ -n "${BINARY_ARGS}" ]; then\n')
                out.write('      COMBINED_ARGS="${MODE_ARGS} ${BINARY_ARGS}"\n')
                out.write('    else\n')
                out.write('      COMBINED_ARGS="${MODE_ARGS}"\n')
                out.write('    fi\n')
                out.write('    skip_remaining_runs=0\n')
                out.write('    for RUN_IDX in $(seq 1 %d); do\n' % runs)
                out.write('      if [ "$skip_remaining_runs" -eq 1 ]; then\n')
                out.write('        echo "Skipping formula=${formula}, mode=${mode}, run=${RUN_IDX} (previous run failed)"\n')
                out.write('        continue\n')
                out.write('      fi\n')
                out.write('      echo "Running formula=${formula}, mode=${mode}, run=${RUN_IDX}, host=$(hostname)"\n')
                out.write('      python3 "${JOB_RUNNER:-./job_runner.py}" ')
                out.write('--binary "${BINARY}" ')
                out.write('--binary-args "${COMBINED_ARGS}" ')
                out.write('--singularity "${SINGULARITY}" ')
                out.write('--mode "${mode}" ')
                out.write('--formula "${formula}" ')
                out.write('--partition "${partition}" ')
                out.write('--run-idx "${RUN_IDX}" ')
                out.write('--out-dir "${OUT_DIR}" ')
                out.write('--timeout "${TIMEOUT}"\n')
                out.write('      exit_code=$?\n')
                out.write('      if [ "$exit_code" -ne 0 ]; then\n')
                out.write('        echo "Run failed with exit code ${exit_code}, skipping remaining runs and this mode for all future patterns"\n')
                out.write('        skip_remaining_runs=1\n')
                out.write('        skip_mode[$mode]=1\n')
                out.write('      fi\n')
                out.write('    done\n')
                out.write('  done\n')
                out.write('done\n')

            os.chmod(job_script, 0o755)
            return job_script

        # Build and submit the global job script
        global_script = write_global_job_script(run_out_dir, [str(p) for p in examples], modes, args.binary, args.singularity, args.binary_args, args.timeout, args.runs, sbatch_opts)
        jobid = submit_script(
            global_script,
            retries=args.sbatch_retries,
            retry_delay=args.sbatch_retry_delay,
            retry_backoff=args.sbatch_retry_backoff,
        )
        submitted.append(jobid)
        expected_runs = len(examples) * len(modes) * args.runs
    else:
        for pattern in examples:
            partition_file = pattern.with_suffix('.part')
            if not partition_file.exists():
                print(f"Skipping {pattern.name}: partition file not found: {partition_file}")
                continue
            for mode in modes:
                job_script = write_job_script(
                    run_out_dir,
                    pattern,
                    partition_file,
                    args.binary,
                    args.singularity,
                    args.binary_args,
                    args.timeout,
                    args.runs,
                    sbatch_opts,
                    mode,
                    single_node=args.single_node,
                )
                jobid = submit_script(
                    job_script,
                    retries=args.sbatch_retries,
                    retry_delay=args.sbatch_retry_delay,
                    retry_backoff=args.sbatch_retry_backoff,
                )
                submitted.append(jobid)
                expected_runs += args.runs

    print(f"Submitted {len(submitted)} jobs ({expected_runs} runs total)")

    if args.wait:
        print('Waiting for results to appear...')
        ok = wait_for_results(run_out_dir, expected_runs, timeout_s=max(3600, args.runs * len(examples) * args.timeout * 2))
        if not ok:
            print('Timeout while waiting for results. Some runs may be missing.')
        else:
            print('All expected result files are present.')

    if args.aggregate:
        aggregated_file = run_out_dir / 'aggregated_results.json'
        aggregate(run_out_dir, aggregated_file)

    print('Done.')


if __name__ == '__main__':
    main()
