Slurm benchmarking for LydiaSyftPlus
=================================

This directory contains a small Slurm-based benchmarking framework that:

- Discovers pattern examples (e.g. `pattern_*.ltlfplus`) under `examples/frompaper` (or a custom directory),
- Submits one Slurm array job per pattern; each array task runs the binary once and writes a per-run JSON result,
- Aggregates per-run JSONs into a single file if requested.

Files
-----

- `submit_bench.py` — discover examples and submit Slurm jobs. Creates `OUT_DIR/jobs`, `OUT_DIR/logs`, and `OUT_DIR/results`.
 - `submit_bench.py` — discover examples and submit Slurm jobs. Creates `OUT_DIR/jobs`, `OUT_DIR/logs`, and `OUT_DIR/results`. Supports iterating solver modes and obligation-solver ids.
- `job_slurm.sh.template` — a small job script template used by `submit_bench.py`.
- `job_runner.py` — executes the binary (optionally inside a Singularity image), measures elapsed time and peak RSS and writes a per-run JSON.
- `aggregate_results.py` — merge per-run JSON files into a single aggregated JSON.

Quick start
-----------

1. Prepare environment: ensure Slurm (`sbatch`) is available on your cluster and `python3` is present on compute nodes. If you want to run the binary inside a Singularity image, have the image accessible.

2. Example: submit benchmarks for all `pattern_*` examples using 5 runs each, waiting for completion and aggregating results:

```bash
python3 scripts/slurm_benchmark/submit_bench.py \
  --runs 5 \
  --wait --aggregate \
  --binary LydiaSyftEL \
  --singularity /path/to/lydiasyftplus.sif \
  --out-dir bench_out
```

To iterate specific solver modes, use `--modes`. Modes are: `el` (EL solver), `cl` (classic Buchi), `pm` (Piterman), `wg` (weak-game/SCC), `cb` (CoBuchi). Optimised modes automatically set `--obligation-simplification 1`.

Example: run only EL and classic modes:

```bash
python3 scripts/slurm_benchmark/submit_bench.py \
  --modes el,cl \
  --runs 3 \
  --singularity /path/to/lydiasyftplus.sif \
  --out-dir bench_out
```
```

Notes
-----

- The submitter creates Slurm array jobs (`--array=1-N`) where N == runs. Each array task writes a file `OUT_DIR/results/<pattern>.run<k>.json`.
- `job_runner.py` measures peak RSS (kilobytes) via Python's `resource.getrusage(RUSAGE_CHILDREN)` and elapsed time via monotonic timers.
- The framework intentionally writes one JSON file per run to avoid locking. After all runs are done, use the `--aggregate` flag or `aggregate_results.py` to merge them.

Future improvements
-------------------

- Use `sacct` for richer Slurm accounting when available (job-level MaxRSS, CPU, etc.).
- Support running multiple solver configurations per pattern (e.g., varying `-g`/`-b` args) and richer summary tables/plots.
- Add optional checksum / lockfile approach to make submitter resilient to retries.

If you want, I can:

- Add optional sacct parsing integration,
- Add plotting/LaTeX export to mimic `benchmark_optimised_compare.py`, or
- Add a driver to run multiple solver configurations for each pattern and collate into the same aggregated structure.
