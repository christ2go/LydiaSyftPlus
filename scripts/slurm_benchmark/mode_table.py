#!/usr/bin/env python3
"""
mk_mode_table.py

Scan a directory of JSON result files and print a table:
number | <mode1> | <mode2> | ...
Rules:
 - If any run for (pattern,mode) has a non-zero exit code, DO NOT show runtime.
   Instead show: X(<code1,code2,...>) ; if SIGSEGV is detected include SIGSEGV in the list.
 - If all runs have returncode == 0 or missing, show average elapsed (with count if >1).
 - Append "x" if any run timed out.
 - Append "X" if any run indicates SIGSEGV (stderr/stdout contains segfault text or rc -11/139).
 - Output is colorised by default; disable with --no-color.

Usage:
    python mk_mode_table.py [-r] [--no-color] /path/to/json_dir
"""
from __future__ import annotations
import argparse
import json
import os
import re
from collections import defaultdict
from typing import Dict, List, Tuple, Any
import math

PATTERN_NUM_RE = re.compile(r"pattern[_\-](\d+)", re.IGNORECASE)
ANSI_RE = re.compile(r'\x1b\[[0-9;]*m')

# ANSI color helpers
class Colors:
    RED = '\033[31m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    CYAN = '\033[36m'
    BOLD = '\033[1m'
    RESET = '\033[0m'
    @staticmethod
    def wrap(s: str, color: str, use_color: bool):
        return f"{color}{s}{Colors.RESET}" if use_color else s

def strip_ansi(s: str) -> str:
    return ANSI_RE.sub('', s)

def find_pattern_number(data: dict, filename: str) -> str:
    pf = data.get("pattern_file") or ""
    m = PATTERN_NUM_RE.search(pf)
    if m:
        return m.group(1)
    m2 = PATTERN_NUM_RE.search(filename)
    if m2:
        return m2.group(1)
    return "UNK"

def looks_like_sigsegv(returncode: Any, stdout: str, stderr: str) -> bool:
    if isinstance(returncode, int) and returncode in (-11, 139):
        return True
    txt = ((stderr or "") + "\n" + (stdout or "")).lower()
    return ("segmentation fault" in txt) or ("segfault" in txt)

def collect(dirpath: str, recursive: bool=False) -> Tuple[Dict[Tuple[str,str], List[Dict[str,Any]]], List[Tuple[str,str]]]:
    files = []
    if recursive:
        for root, _, fnames in os.walk(dirpath):
            for f in fnames:
                if f.lower().endswith(".json"):
                    files.append(os.path.join(root, f))
    else:
        for f in sorted(os.listdir(dirpath)):
            if f.lower().endswith(".json"):
                files.append(os.path.join(dirpath, f))

    bucket = defaultdict(list)
    parse_errors = []

    for p in files:
        try:
            with open(p, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except Exception as e:
            parse_errors.append((p, str(e)))
            continue

        fname = os.path.basename(p)
        pattern_num = find_pattern_number(data, fname)
        mode = data.get("mode") or "MISSING"
        elapsed = data.get("elapsed_s")
        elapsed_v = None
        if isinstance(elapsed, (int, float)):
            elapsed_v = float(elapsed)

        timeout = bool(data.get("timeout", False))
        rc = data.get("returncode")
        # try to normalize ints from strings
        if isinstance(rc, str) and rc.isdigit():
            rc = int(rc)
        stdout = data.get("stdout", "") or ""
        stderr = data.get("stderr", "") or ""
        sig = looks_like_sigsegv(rc, stdout, stderr)

        bucket[(pattern_num, mode)].append({
            "elapsed": elapsed_v,
            "timeout": timeout,
            "rc": rc,
            "sig": sig,
            "file": p
        })
    return bucket, parse_errors

def build_cell(runs: List[Dict[str,Any]], use_color: bool) -> str:
    # Deprecated signature kept for backward-compat; delegate to new signature
    return build_cell_with_time(runs, use_color, time_source="elapsed")


def build_cell_with_time(runs: List[Dict[str,Any]], use_color: bool, time_source: str) -> str:
    # collect return codes (non-None)
    rc_set = set()
    for r in runs:
        if r["rc"] is None:
            continue
        try:
            # coerce possible floats that are ints
            rc_val = int(r["rc"])
            rc_set.add(rc_val)
        except Exception:
            # non-int codes (unlikely) keep raw
            rc_set.add(r["rc"])
    # detect segfaults
    sig_any = any(r["sig"] for r in runs)
    timeout_any = any(r["timeout"] for r in runs)

    # If any non-zero return code present (and not None and not zero) -> SUPPRESS time
    nonzero_codes = sorted([c for c in rc_set if isinstance(c, int) and c != 0] +
                           [c for c in rc_set if not isinstance(c, int) and c != 0])
    # But if rc_set is empty or only 0, treat as successful runs
    if nonzero_codes:
        # include SIGSEGV label if present and not already in the codes
        code_labels = []
        if sig_any:
            code_labels.append("SIGSEGV")
        # append numeric codes as strings (avoid duplicating SIGSEGV if its numeric code included)
        for c in nonzero_codes:
            # if c corresponds to segv codes (-11,139) we may have added SIGSEGV already, still include numeric if asked
            code_labels.append(str(c))
        label = ",".join(code_labels)
        cell = f"X({label})"
        cell = Colors.wrap(cell, Colors.RED, use_color)
        # if also timeouts present, append small yellow 'x'
        if timeout_any:
            cell += " " + Colors.wrap("x", Colors.YELLOW, use_color)
        return cell

    # else all rc == 0 or missing
    def get_time(r: Dict[str, Any]) -> Any:
        # If user requested wall/cpu, prefer parsed stdout values unless the run timed out
        if time_source in ("wall", "cpu") and not r.get("timeout", False):
            # try parse from stdout
            txt = r.get("stdout", "") or ""
            wall_m = re.search(r"Wall time:\s*([0-9]+\.?[0-9]*)\s*seconds", txt)
            cpu_m = re.search(r"CPU time:\s*([0-9]+\.?[0-9]*)\s*seconds", txt)
            if time_source == "wall" and wall_m:
                try:
                    return float(wall_m.group(1))
                except Exception:
                    pass
            if time_source == "cpu" and cpu_m:
                try:
                    return float(cpu_m.group(1))
                except Exception:
                    pass
        # fallback to elapsed field
        return r.get("elapsed")

    elapsed_vals = [get_time(r) for r in runs if isinstance(get_time(r), (int, float))]
    cell_parts = []
    if elapsed_vals:
        avg = sum(elapsed_vals) / len(elapsed_vals)
        if len(elapsed_vals) != len(runs):
            # some runs didn't report elapsed: show count as n_with_time/total
            time_str = f"{avg:.3f}s ({len(elapsed_vals)}/{len(runs)})"
        else:
            time_str = f"{avg:.3f}s" + (f" ({len(runs)})" if len(runs) > 1 else "")
        time_str = Colors.wrap(time_str, Colors.GREEN, use_color)
        cell_parts.append(time_str)
    else:
        cell_parts.append("-")

    # append timeout mark if any
    if timeout_any:
        cell_parts.append(Colors.wrap("x", Colors.YELLOW, use_color))
    # append SIGSEGV uppercase mark if any (even if returncodes are zero but evidence present)
    if sig_any:
        cell_parts.append(Colors.wrap("X", Colors.RED, use_color))

    return " ".join(cell_parts)


LATEX_MODE_ORDER = ["wg", "el", "pm", "cl", "sr", "cb", "ltlf"]
LATEX_MODE_NAMES = {
    "wg": "SCC",
    "el": "EL",
    "pm": "SafeReach",
    "cl": "Buechi",
    "cb": "CoBuechi",
    "ltlf": "LTLf",
}


def format_latex_cell(runs: List[Dict[str, Any]], show_ci: bool) -> str:
    # Error / OOM / non-zero return -> ddagger, but any timeout wins with dagger
    rc_set = set()
    for r in runs:
        rc = r["rc"]
        if isinstance(rc, str) and rc.isdigit():
            rc = int(rc)
        if rc is not None:
            rc_set.add(rc)

    timeout_any = any(r["timeout"] for r in runs)

    if timeout_any:
        return r"\textsc{$\dagger$}"

    nonzero_codes = [c for c in rc_set if isinstance(c, int) and c != 0] + [c for c in rc_set if not isinstance(c, int) and c not in (None, 0)]
    if nonzero_codes:
        return r"\textsc{$\ddagger$}"

    elapsed_vals = [r["elapsed"] for r in runs if isinstance(r["elapsed"], (int, float))]
    if not elapsed_vals:
        # treat missing as timeout
        return r"\textsc{$\dagger$}"

    avg = sum(elapsed_vals) / len(elapsed_vals)
    cell = f"\\textbf{{{avg:.3f}s}}"
    if show_ci and len(elapsed_vals) > 1:
        # sample std dev
        mean = avg
        var = sum((x - mean) ** 2 for x in elapsed_vals) / len(elapsed_vals)
        std = math.sqrt(var)
        cell += f" \\scriptsize $\\pm${std:.3f}s"

    return cell


def make_table_latex(bucket: Dict[Tuple[str, str], List[Dict[str, Any]]], parse_errors: List[Tuple[str, str]], show_ci: bool):
    pattern_nums = set()
    modes = set()
    for (pat, mode) in bucket.keys():
        pattern_nums.add(pat)
        modes.add(mode)

    if not pattern_nums:
        print("No JSON result files found.")
        if parse_errors:
            print("% Parse errors:")
            for p, err in parse_errors:
                print("% -", p, ":", err)
        return

    def pat_key(x):
        try:
            return (0, int(x))
        except Exception:
            return (1, x)

    sorted_patterns = sorted(pattern_nums, key=pat_key)

    # Determine mode order: use predefined order then append unknowns
    ordered_modes = [m for m in LATEX_MODE_ORDER if m in modes]
    unknown_modes = [m for m in sorted(modes) if m not in ordered_modes]
    ordered_modes.extend(unknown_modes)

    col_headers = [LATEX_MODE_NAMES.get(m, m) for m in ordered_modes]

    # Begin LaTeX table
    cols_spec = "r|" + "c" * len(ordered_modes)
    print(f"\\begin{{tabular}}{{{cols_spec}}}")
    print("\\hline")
    header_line = "$n$" + " & " + " & ".join(col_headers) + " \\\\"
    print(header_line)
    print("\\hline")

    for pat in sorted_patterns:
        cells = [pat]
        for mode in ordered_modes:
            runs = bucket.get((pat, mode), [])
            cell = format_latex_cell(runs, show_ci=show_ci) if runs else r"\textsc{$\dagger$}"
            cells.append(cell)
        print(" ".join([str(cells[0])]) + " & " + " & ".join(cells[1:]) + r" \\")

    print("\\hline")
    print("\\end{tabular}")

    if parse_errors:
        print("% Parse errors:")
        for p, err in parse_errors:
            print(f"% - {p}: {err}")

def make_table(bucket: Dict[Tuple[str,str], List[Dict[str,Any]]], parse_errors: List[Tuple[str,str]], use_color: bool):
    pattern_nums = set()
    modes = set()
    for (pat, mode) in bucket.keys():
        pattern_nums.add(pat)
        modes.add(mode)

    if not pattern_nums:
        print("No JSON result files found.")
        if parse_errors:
            print("\nParse errors:")
            for p, err in parse_errors:
                print(" -", p, ":", err)
        return

    def pat_key(x):
        try:
            return (0, int(x))
        except Exception:
            return (1, x)
    sorted_patterns = sorted(pattern_nums, key=pat_key)
    sorted_modes = sorted(modes)

    header = ["number"] + sorted_modes
    # prepare rows
    rows = []
    for pat in sorted_patterns:
        row = [pat]
        for mode in sorted_modes:
            runs = bucket.get((pat, mode), [])
            if not runs:
                cell = "-"
            else:
                cell = build_cell(runs, use_color)
            row.append(cell)
        rows.append(row)

    # compute col widths considering ANSI codes (use strip_ansi)
    col_widths = []
    for cidx in range(len(header)):
        maxw = max(len(strip_ansi(header[cidx])), max((len(strip_ansi(row[cidx])) for row in rows), default=0))
        col_widths.append(max(maxw, len(header[cidx])))

    # print header
    hdr_line = " | ".join(header[i].ljust(col_widths[i]) for i in range(len(header)))
    sep_line = "-|-".join("-" * col_widths[i] for i in range(len(header)))
    print(hdr_line)
    print(sep_line)
    for r in rows:
        line = " | ".join(r[i].ljust(col_widths[i] + (len(r[i]) - len(strip_ansi(r[i])))) for i in range(len(r)))
        print(line)

    if parse_errors:
        print("\n# Parse errors (files that couldn't be read):")
        for p, err in parse_errors:
            print(f"# - {p}: {err}")

def main():
    ap = argparse.ArgumentParser(description="Generate mode table with runtimes / timeouts / SIGSEGV marks.")
    ap.add_argument("dir", help="Directory with JSON files")
    ap.add_argument("-r", "--recursive", action="store_true", help="Search directory recursively")
    ap.add_argument("--no-color", action="store_true", help="Disable color output")
    ap.add_argument("--latex", action="store_true", help="Output LaTeX tabular instead of plain text")
    ap.add_argument("--latex-ci", action="store_true", help="Show ±stddev in LaTeX output when multiple runs")
    args = ap.parse_args()

    bucket, parse_errors = collect(args.dir, recursive=args.recursive)
    if args.latex:
        make_table_latex(bucket, parse_errors, show_ci=args.latex_ci)
    else:
        make_table(bucket, parse_errors, use_color=not args.no_color)

if __name__ == "__main__":
    main()
