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


TEMPLATE = Path(__file__).parent / 'job_slurm.sh.template'


def find_examples(examples_dir, prefix='pattern'):
    p = Path(examples_dir)
    if not p.exists():
        raise FileNotFoundError(f"Examples directory not found: {examples_dir}")
    files = sorted([f for f in p.iterdir() if f.is_file() and f.name.startswith(prefix + '_') and f.name.endswith('.ltlfplus')],
                   key=lambda x: int(x.name.replace(prefix + '_', '').replace('.ltlfplus', '')))
    return files


def write_job_script(out_dir, pattern_file, partition_file, binary, singularity, binary_args, timeout, runs, sbatch_opts, mode):
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
        # array directive
        out.write(f"#SBATCH --array=1-{runs}\n")
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
            mode_args = '-s 0 -g 0'
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
        out.write(f"OUT_DIR=\"{out_dir / 'results'}\"\n")
        out.write(f"TIMEOUT=\"{timeout}\"\n")
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
        out.write(tmpl_body)

    os.chmod(job_script, 0o755)
    return job_script


def submit_script(script_path):
    # Submit with sbatch and return job id (parsable form)
    try:
        res = subprocess.run(['sbatch', '--parsable', str(script_path)], capture_output=True, text=True, check=True)
        jobid = res.stdout.strip()
        print(f"Submitted {script_path.name} -> job {jobid}")
        return jobid
    except subprocess.CalledProcessError as e:
        print('sbatch failed:', e.stderr)
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
    parser.add_argument('--sbatch-mem', default='4G', help='Slurm memory per task')
    parser.add_argument('--sbatch-cpus', default='1', help='Slurm cpus-per-task')
    parser.add_argument('--sbatch-partition', default='', help='Slurm partition (optional)')
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
    (out_dir / 'results').mkdir(parents=True, exist_ok=True)
    (out_dir / 'logs').mkdir(parents=True, exist_ok=True)
    (out_dir / 'jobs').mkdir(parents=True, exist_ok=True)

    sbatch_opts = {
        '--time': args.sbatch_time,
        '--mem': args.sbatch_mem,
        '--cpus-per-task': args.sbatch_cpus,
    }
    if args.sbatch_partition:
        sbatch_opts['--partition'] = args.sbatch_partition

    modes = [m.strip() for m in args.modes.split(',') if m.strip()]

    submitted = []
    expected_runs = 0
    for pattern in examples:
        partition_file = pattern.with_suffix('.part')
        if not partition_file.exists():
            print(f"Skipping {pattern.name}: partition file not found: {partition_file}")
            continue

        for mode in modes:
            job_script = write_job_script(out_dir, pattern, partition_file, args.binary, args.singularity, args.binary_args, args.timeout, args.runs, sbatch_opts, mode)
            jobid = submit_script(job_script)
            submitted.append(jobid)
            expected_runs += args.runs

    print(f"Submitted {len(submitted)} jobs ({expected_runs} runs total)")

    if args.wait:
        print('Waiting for results to appear...')
        ok = wait_for_results(out_dir, expected_runs, timeout_s=max(3600, args.runs * len(examples) * args.timeout * 2))
        if not ok:
            print('Timeout while waiting for results. Some runs may be missing.')
        else:
            print('All expected result files are present.')

    if args.aggregate:
        aggregated_file = out_dir / 'aggregated_results.json'
        aggregate(out_dir, aggregated_file)

    print('Done.')


if __name__ == '__main__':
    main()
