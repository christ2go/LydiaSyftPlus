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
from pathlib import Path


TEMPLATE = Path(__file__).parent / 'job_slurm.sh.template'


def find_examples(examples_dir, prefix='pattern'):
    p = Path(examples_dir)
    if not p.exists():
        raise FileNotFoundError(f"Examples directory not found: {examples_dir}")
    files = sorted([f for f in p.iterdir() if f.is_file() and f.name.startswith(prefix + '_') and f.name.endswith('.ltlfplus')],
                   key=lambda x: int(x.name.replace(prefix + '_', '').replace('.ltlfplus', '')))
    return files


def write_job_script(out_dir, pattern_file, partition_file, binary, singularity, binary_args, timeout, runs, sbatch_opts):
    # Create a job script under out_dir/jobs
    job_name = pattern_file.stem
    jobs_dir = out_dir / 'jobs'
    jobs_dir.mkdir(parents=True, exist_ok=True)
    logs_dir = out_dir / 'logs'
    logs_dir.mkdir(parents=True, exist_ok=True)

    job_script = jobs_dir / f"job_{job_name}.sh"
    with open(TEMPLATE, 'r') as t, open(job_script, 'w') as out:
        # Write SBATCH header
        out.write('#!/usr/bin/env bash\n')
        out.write(f"#SBATCH --job-name={job_name}\n")
        out.write(f"#SBATCH --output={logs_dir}/{job_name}-%A_%a.out\n")
        # append other sbatch opts
        for k, v in sbatch_opts.items():
            out.write(f"#SBATCH {k}={v}\n")
        # array directive
        out.write(f"#SBATCH --array=1-{runs}\n")
        out.write('\n')

        # Export environment variables that template expects
        out.write(f"FORMULA_FILE=\"{pattern_file}\"\n")
        out.write(f"PARTITION_FILE=\"{partition_file}\"\n")
        out.write(f"BINARY=\"{binary}\"\n")
        out.write(f"SINGULARITY=\"{singularity or ''}\"\n")
        out.write(f"BINARY_ARGS=\"{binary_args or ''}\"\n")
        out.write(f"OUT_DIR=\"{out_dir / 'results'}\"\n")
        out.write(f"TIMEOUT=\"{timeout}\"\n")
        out.write(f"RUN_IDX=\"${{SLURM_ARRAY_TASK_ID}}\"\n")
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

    submitted = []
    expected_runs = 0
    for pattern in examples:
        partition_file = pattern.with_suffix('.part')
        if not partition_file.exists():
            print(f"Skipping {pattern.name}: partition file not found: {partition_file}")
            continue
        job_script = write_job_script(out_dir, pattern, partition_file, args.binary, args.singularity, args.binary_args, args.timeout, args.runs, sbatch_opts)
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
