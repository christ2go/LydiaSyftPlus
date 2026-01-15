#!/usr/bin/env python3
"""
Random obligation-fragment LTLf+ head-to-head speed test with live curses UI.

Compares runtime:
  - normal: --obligation-simplification 0
  - oblig : --obligation-simplification 1

Rules:
- If either run times out (or both): show both times and CONTINUE.
- If both runs finish:
    - If --require-same-answer and both contain a realizability verdict and they disagree: EXIT nonzero.
- If the first measurement shows >2.0x differential in either direction:
    - rerun the same instance 2 more times (3 total),
    - if the median still shows >2.0x, save the instance (formula+partition+timings).

UI:
- curses live screen with current formula, partition, progress bar, stats.
"""

import argparse
import os
import random
import subprocess
import tempfile
import time
import curses
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, List, Tuple, Dict

ATOMS = ["p", "q", "r", "s", "t"]


# ----------------------------
# Random formula generation
# ----------------------------

def random_ltlf_formula(depth: int, atoms: List[str]) -> str:
    if depth <= 0 or random.random() < 0.30:
        atom = random.choice(atoms)
        return f"!{atom}" if random.random() < 0.30 else atom

    op = random.choice(["and", "or", "X", "F", "G", "U", "impl"])

    if op == "and":
        return f"({random_ltlf_formula(depth-1, atoms)} & {random_ltlf_formula(depth-1, atoms)})"
    if op == "or":
        return f"({random_ltlf_formula(depth-1, atoms)} | {random_ltlf_formula(depth-1, atoms)})"
    if op == "X":
        return f"X({random_ltlf_formula(depth-1, atoms)})"
    if op == "F":
        return f"F({random_ltlf_formula(depth-1, atoms)})"
    if op == "G":
        return f"G({random_ltlf_formula(depth-1, atoms)})"
    if op == "U":
        return f"({random_ltlf_formula(depth-1, atoms)} U {random_ltlf_formula(depth-1, atoms)})"
    if op == "impl":
        return f"({random_ltlf_formula(depth-1, atoms)} -> {random_ltlf_formula(depth-1, atoms)})"

    return random.choice(atoms)


def random_obligation_formula(boolean_depth: int, inner_depth: int, atoms: List[str]) -> str:
    """
    Obligation fragment: positive boolean combos of A(phi) / E(phi),
    where phi is an LTLf formula.
    """
    def rec(d: int) -> str:
        if d <= 0 or random.random() < 0.40:
            inner = random_ltlf_formula(inner_depth, atoms)
            return f"A({inner})" if random.random() < 0.50 else f"E({inner})"
        op = random.choice(["&", "|"])
        left = rec(d - 1)
        right = rec(d - 1)
        return f"({left} {op} {right})"
    return rec(boolean_depth)


def random_partition(atoms: List[str]) -> Tuple[List[str], List[str]]:
    a = list(atoms)
    random.shuffle(a)
    if len(a) < 2:
        return a, []
    split = random.randint(1, len(a) - 1)
    return a[:split], a[split:]


# ----------------------------
# IO helpers
# ----------------------------

def write_formula_file(formula: str, filepath: str) -> None:
    with open(filepath, "w") as f:
        f.write(formula + "\n")


def write_partition_file(inputs: List[str], outputs: List[str], filepath: str) -> None:
    with open(filepath, "w") as f:
        f.write(".inputs: " + " ".join(inputs) + "\n")
        f.write(".outputs: " + " ".join(outputs) + "\n")


def parse_realizability(output: str) -> Optional[bool]:
    up = output.upper()
    if "UNREALIZABLE" in up:
        return False
    if "REALIZABLE" in up:
        return True
    return None


# ----------------------------
# Running + formatting
# ----------------------------

@dataclass
class RunResult:
    status: str              # "ok" | "timeout" | "error"
    elapsed_s: float
    realizable: Optional[bool]
    output: str


def _force_text(x) -> str:
    if x is None:
        return ""
    if isinstance(x, bytes):
        return x.decode(errors="replace")
    return str(x)


def run_solver(
    binary_path: str,
    formula_file: str,
    partition_file: str,
    obligation_simplification: int,
    solver_id: str,
    timeout_s: int,
) -> RunResult:
    cmd = [
        binary_path,
        "-i", formula_file,
        "-p", partition_file,
        "-s", "0",
        "-g", solver_id,
        "--obligation-simplification", str(obligation_simplification),
    ]

    t0 = time.perf_counter()
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
        t1 = time.perf_counter()
        out = _force_text(cp.stdout) + _force_text(cp.stderr)
        return RunResult("ok", t1 - t0, parse_realizability(out), out)
    except subprocess.TimeoutExpired as e:
        t1 = time.perf_counter()
        out = _force_text(getattr(e, "stdout", None)) + _force_text(getattr(e, "stderr", None))
        if not out:
            out = "TIMEOUT"
        return RunResult("timeout", t1 - t0, None, out)
    except Exception as e:
        t1 = time.perf_counter()
        return RunResult("error", t1 - t0, None, str(e))


def fmt_duration(seconds: Optional[float], *, timeout_s: Optional[int] = None, status: str = "ok") -> str:
    if seconds is None:
        return "N/A"
    if status == "timeout" and timeout_s is not None:
        seconds = float(timeout_s)

    # choose unit so value in [1,100) if possible
    units = [("us", 1e-6), ("ms", 1e-3), ("s", 1.0)]
    chosen = None
    for name, scale in units:
        v = seconds / scale
        if 1.0 <= v < 100.0:
            chosen = (name, v)
            break
    if chosen is None:
        v_us = seconds / 1e-6
        chosen = ("us", v_us) if v_us < 1.0 else ("s", seconds)

    name, v = chosen
    # compact formatting
    if v < 10.0:
        s = f"{v:.1f}{name}"
    else:
        s = f"{v:.0f}{name}"

    if status == "timeout" and timeout_s is not None:
        return f">={s}"
    return s


def ratio_from_times(normal_s: float, oblig_s: float) -> float:
    """normal/oblig (so >1 means oblig faster)."""
    return normal_s / max(oblig_s, 1e-12)


# ----------------------------
# Measurement logic
# ----------------------------

@dataclass
class PairResult:
    normal: RunResult
    oblig: RunResult


def run_pair(args, formula: str, ins: List[str], outs: List[str]) -> PairResult:
    with tempfile.TemporaryDirectory() as td:
        f_formula = os.path.join(td, "test.ltlfplus")
        f_part = os.path.join(td, "test.part")
        write_formula_file(formula, f_formula)
        write_partition_file(ins, outs, f_part)
        normal = run_solver(args.binary, f_formula, f_part, 0, args.g, args.timeout)
        oblig = run_solver(args.binary, f_formula, f_part, 1, args.g, args.timeout)
        return PairResult(normal=normal, oblig=oblig)


@dataclass
class MeasureOutcome:
    pairs: List[PairResult]          # length 1 or 3
    comparable: bool                 # both ok (on the first pair)
    had_timeout: bool                # timeout in any run (on the first pair)
    had_error: bool                  # error in first pair
    median_ratio: Optional[float]    # median ratio if 3 ok pairs exist
    repeated: bool                   # whether we repeated
    saved: bool                      # whether saved due to >2x median


def maybe_repeat_and_save(
    args,
    idx: int,
    formula: str,
    ins: List[str],
    outs: List[str],
    saver: "Saver",
) -> MeasureOutcome:
    pairs: List[PairResult] = []

    first = run_pair(args, formula, ins, outs)
    pairs.append(first)

    # first-pair error => global error-out handled by caller
    if first.normal.status == "error" or first.oblig.status == "error":
        return MeasureOutcome(pairs, comparable=False, had_timeout=False, had_error=True,
                              median_ratio=None, repeated=False, saved=False)

    # first-pair timeouts => just continue (no repeats)
    if first.normal.status == "timeout" or first.oblig.status == "timeout":
        return MeasureOutcome(pairs, comparable=False, had_timeout=True, had_error=False,
                              median_ratio=None, repeated=False, saved=False)

    # comparable
    r1 = ratio_from_times(first.normal.elapsed_s, first.oblig.elapsed_s)
    big_swing = (r1 > args.rerun_threshold) or (r1 < 1.0 / args.rerun_threshold)

    if not big_swing:
        return MeasureOutcome(pairs, comparable=True, had_timeout=False, had_error=False,
                              median_ratio=None, repeated=False, saved=False)

    # repeat twice more (3 total)
    repeated_pairs = 2
    for _ in range(repeated_pairs):
        pr = run_pair(args, formula, ins, outs)
        pairs.append(pr)

        # If any of the repeat runs error, treat as error-ish but don't crash; caller can decide.
        if pr.normal.status == "error" or pr.oblig.status == "error":
            return MeasureOutcome(pairs, comparable=True, had_timeout=False, had_error=True,
                                  median_ratio=None, repeated=True, saved=False)
        # If any timeout during repeats, don't save; we still return repeated=True.
        if pr.normal.status == "timeout" or pr.oblig.status == "timeout":
            return MeasureOutcome(pairs, comparable=True, had_timeout=True, had_error=False,
                                  median_ratio=None, repeated=True, saved=False)

    # All 3 ok: compute median ratio using medians of times (more stable)
    normals = [p.normal.elapsed_s for p in pairs]
    obligs = [p.oblig.elapsed_s for p in pairs]
    med_n = statistics.median(normals)
    med_o = statistics.median(obligs)
    med_ratio = ratio_from_times(med_n, med_o)

    still_big = (med_ratio > args.rerun_threshold) or (med_ratio < 1.0 / args.rerun_threshold)
    saved = False
    if still_big:
        saver.save_case(idx, formula, ins, outs, pairs, med_ratio)
        saved = True

    return MeasureOutcome(pairs, comparable=True, had_timeout=False, had_error=False,
                          median_ratio=med_ratio, repeated=True, saved=saved)


# ----------------------------
# Saving outliers
# ----------------------------

class Saver:
    def __init__(self, out_dir: Path):
        self.out_dir = out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)

    def save_case(
        self,
        idx: int,
        formula: str,
        ins: List[str],
        outs: List[str],
        pairs: List[PairResult],
        med_ratio: float,
    ) -> None:
        # Unique-ish name: include index + time-based salt
        salt = int(time.time() * 1000) % 1_000_000
        base = f"case{idx:04d}_{salt:06d}"
        f_formula = self.out_dir / f"{base}.ltlfplus"
        f_part = self.out_dir / f"{base}.part"
        f_report = self.out_dir / f"{base}.txt"

        f_formula.write_text(formula + "\n")
        f_part.write_text(
            ".inputs: " + " ".join(ins) + "\n" +
            ".outputs: " + " ".join(outs) + "\n"
        )

        lines = []
        lines.append(f"index: {idx}")
        lines.append(f"median_ratio (normal/oblig): {med_ratio:.3f}x")
        lines.append(f"threshold: >{args_global_rerun_threshold}x or <{1.0/args_global_rerun_threshold:.3f}x")
        lines.append("")
        lines.append("formula:")
        lines.append(formula)
        lines.append("")
        lines.append(".inputs: " + " ".join(ins))
        lines.append(".outputs: " + " ".join(outs))
        lines.append("")
        lines.append("runs (seconds):")
        for j, pr in enumerate(pairs, 1):
            lines.append(
                f"  run{j}: normal={pr.normal.elapsed_s:.6f}s ({pr.normal.status}) "
                f"oblig={pr.oblig.elapsed_s:.6f}s ({pr.oblig.status}) "
                f"ratio={ratio_from_times(pr.normal.elapsed_s, pr.oblig.elapsed_s):.3f}x"
            )
        lines.append("")
        lines.append("verdicts (if parsed):")
        lines.append(f"  normal: {pairs[-1].normal.realizable}")
        lines.append(f"  oblig : {pairs[-1].oblig.realizable}")

        f_report.write_text("\n".join(lines) + "\n")


# (hacky but simple) used in Saver report so it doesn't need args passed
args_global_rerun_threshold = 2.0


# ----------------------------
# curses UI
# ----------------------------

def _clip(s: str, width: int) -> str:
    if width <= 0:
        return ""
    return s if len(s) <= width else s[: max(0, width - 1)] + "…"


def draw_ui(stdscr, state: Dict) -> None:
    stdscr.erase()
    h, w = stdscr.getmaxyx()

    i = state["i"]
    n = state["n"]
    formula = state.get("formula", "")
    ins = state.get("ins", [])
    outs = state.get("outs", [])
    last_msg = state.get("last_msg", "")
    counts = state["counts"]

    # Progress bar
    bar_w = max(10, min(60, w - 20))
    done = i - 1
    frac = 0.0 if n <= 0 else done / n
    fill = int(bar_w * frac)
    bar = "[" + "#" * fill + "-" * (bar_w - fill) + "]"

    line = 0
    stdscr.addstr(line, 0, _clip(f"{bar} {done}/{n} ({frac*100:5.1f}%)", w - 1)); line += 1

    # Stats
    comp = counts["comparable"]
    faster = counts["faster"]
    slower = counts["slower"]
    ties = counts["ties"]
    timeouts = counts["timeouts"]
    saved = counts["saved"]

    pfast = (faster / comp * 100.0) if comp > 0 else 0.0
    stdscr.addstr(line, 0, _clip(
        f"ok={comp}  YAY={faster}  nah={slower}  tie={ties}  timeout={timeouts}  saved={saved}  P(YAY|ok)={pfast:4.1f}%",
        w - 1
    )); line += 2

    # Current instance
    stdscr.addstr(line, 0, _clip(f"Current formula:", w - 1)); line += 1
    for chunk in wrap_text(formula, w - 1, max_lines=3):
        stdscr.addstr(line, 0, chunk); line += 1
        if line >= h - 4:
            break
    if line < h - 3:
        stdscr.addstr(line, 0, _clip(f".inputs:  {' '.join(ins)}", w - 1)); line += 1
        stdscr.addstr(line, 0, _clip(f".outputs: {' '.join(outs)}", w - 1)); line += 2

    # Last message
    if line < h - 2:
        stdscr.addstr(line, 0, _clip(f"Last: {last_msg}", w - 1)); line += 1

    if h >= 1:
        stdscr.addstr(h - 1, 0, _clip("q = quit | Ctrl+C also works", w - 1))

    stdscr.refresh()


def wrap_text(s: str, width: int, max_lines: int = 3) -> List[str]:
    if width <= 1:
        return [_clip(s, width)]
    words = s.split(" ")
    lines = []
    cur = ""
    for w in words:
        if not cur:
            cur = w
        elif len(cur) + 1 + len(w) <= width:
            cur += " " + w
        else:
            lines.append(_clip(cur, width))
            cur = w
            if len(lines) >= max_lines:
                return lines
    if cur and len(lines) < max_lines:
        lines.append(_clip(cur, width))
    return lines


# ----------------------------
# Main
# ----------------------------

def main() -> int:
    global args_global_rerun_threshold

    ap = argparse.ArgumentParser(description="Compare normal vs obligation-simplified runtime on obligation-fragment formulas.")
    ap.add_argument("--binary", "-b", default="./../build/bin/LydiaSyftEL", help="Path to LydiaSyftEL binary")
    ap.add_argument("--n", type=int, default=200, help="Number of instances")
    ap.add_argument("--atoms", "-a", type=int, default=3, help="How many atoms to use (prefix of p,q,r,s,t)")
    ap.add_argument("--bool-depth", type=int, default=2, help="Boolean nesting depth in obligation fragment")
    ap.add_argument("--inner-depth", type=int, default=2, help="Inner LTLf depth inside A()/E()")
    ap.add_argument("--g", default="0", help="Solver id for -g (used for BOTH runs)")
    ap.add_argument("--timeout", type=int, default=60, help="Per-run timeout seconds")
    ap.add_argument("--seed", type=int, default=None, help="Random seed")
    ap.add_argument("--require-same-answer", action="store_true",
                    help="If both runs produce verdicts and they differ: exit immediately")
    ap.add_argument("--truncate", type=int, default=4000, help="Max chars of solver output to print on disagreement")
    ap.add_argument("--rerun-threshold", type=float, default=2.0,
                    help="If first run shows >T x diff either direction, rerun twice and decide by median")
    ap.add_argument("--save-dir", default="speed_diffs", help="Directory to save >2x outliers after reruns")
    ap.add_argument("--no-live", action="store_true", help="Disable curses UI, print lines normally")
    ap.add_argument("--show-slow", action="store_true", help="Print when obligation is slower (non-live mode)")
    ap.add_argument("--show-formula", action="store_true", help="Print formula+partition on YAY (non-live mode)")
    args = ap.parse_args()

    args_global_rerun_threshold = args.rerun_threshold

    if args.seed is not None:
        random.seed(args.seed)

    atoms = ATOMS[: max(1, min(len(ATOMS), args.atoms))]
    saver = Saver(Path(args.save_dir))

    counts = {
        "comparable": 0,
        "faster": 0,
        "slower": 0,
        "ties": 0,
        "timeouts": 0,
        "saved": 0,
    }

    def handle_one(i: int) -> Tuple[str, Optional[int]]:
        """Run one instance. Returns (last_msg, exit_code_or_None)."""
        formula = random_obligation_formula(args.bool_depth, args.inner_depth, atoms)
        ins, outs = random_partition(atoms)

        outcome = maybe_repeat_and_save(args, i, formula, ins, outs, saver)

        # If first pair had error => global error-out
        if outcome.had_error and (outcome.pairs[0].normal.status == "error" or outcome.pairs[0].oblig.status == "error"):
            pr = outcome.pairs[0]
            msg = f"ERROR normal={pr.normal.status} oblig={pr.oblig.status}"
            print(f"[{i}/{args.n}] {msg}")
            print("Formula:", formula)
            print(".inputs:", " ".join(ins))
            print(".outputs:", " ".join(outs))
            print("\n--- normal output (truncated) ---")
            print(pr.normal.output[:args.truncate])
            print("\n--- oblig output (truncated) ---")
            print(pr.oblig.output[:args.truncate])
            return msg, 3

        # Timeouts on first pair => continue
        if outcome.had_timeout and not outcome.comparable:
            pr = outcome.pairs[0]
            counts["timeouts"] += 1
            tn = fmt_duration(pr.normal.elapsed_s, timeout_s=args.timeout, status=pr.normal.status)
            to = fmt_duration(pr.oblig.elapsed_s, timeout_s=args.timeout, status=pr.oblig.status)
            return f"TIMEOUT normal={tn} oblig={to}", None

        # Comparable on first pair
        counts["comparable"] += 1
        pr0 = outcome.pairs[0]

        # Disagreement check (only if both verdicts parsed, and only for the *first* pair)
        if args.require_same_answer:
            if (pr0.normal.realizable is not None) and (pr0.oblig.realizable is not None) and (pr0.normal.realizable != pr0.oblig.realizable):
                msg = f"DISAGREEMENT (normal={pr0.normal.realizable} oblig={pr0.oblig.realizable}) => EXIT"
                print(f"[{i}/{args.n}] {msg}")
                print("Formula:", formula)
                print(".inputs:", " ".join(ins))
                print(".outputs:", " ".join(outs))
                print("normal time:", fmt_duration(pr0.normal.elapsed_s))
                print("oblig  time:", fmt_duration(pr0.oblig.elapsed_s))
                print("\n--- normal output (truncated) ---")
                print(pr0.normal.output[:args.truncate])
                print("\n--- oblig output (truncated) ---")
                print(pr0.oblig.output[:args.truncate])
                return msg, 2

        # If we repeated and saved, count it
        if outcome.saved:
            counts["saved"] += 1

        # If repeats had timeout/error, just report it and continue
        if outcome.repeated and (outcome.had_timeout or outcome.had_error):
            # choose last pair for display
            pr_last = outcome.pairs[-1]
            tn = fmt_duration(pr_last.normal.elapsed_s, timeout_s=args.timeout, status=pr_last.normal.status)
            to = fmt_duration(pr_last.oblig.elapsed_s, timeout_s=args.timeout, status=pr_last.oblig.status)
            if pr_last.normal.status == "timeout" or pr_last.oblig.status == "timeout":
                counts["timeouts"] += 1
                return f"TIMEOUT (during reruns) normal={tn} oblig={to}", None
            return f"ERROR (during reruns) normal={pr_last.normal.status} oblig={pr_last.oblig.status}", None

        # Decide faster/slower/tie based on:
        # - if repeated: median ratio
        # - else: first run ratio
        if outcome.repeated and outcome.median_ratio is not None:
            r = outcome.median_ratio
            tag = "median"
        else:
            r = ratio_from_times(pr0.normal.elapsed_s, pr0.oblig.elapsed_s)
            tag = "run1"

        # times for display: show first run times, plus note rerun/median if applicable
        tn0 = fmt_duration(pr0.normal.elapsed_s)
        to0 = fmt_duration(pr0.oblig.elapsed_s)

        if r > 1.0:
            counts["faster"] += 1
            extra = f" ({tag} {r:.2f}x)"
            extra += " [saved]" if outcome.saved else ""
            msg = f"YAY oblig={to0} normal={tn0}{extra}"
            if (not args.no_live) or args.show_formula:
                # only print formula in non-live if asked
                pass
            if args.no_live:
                out = f"[{i}/{args.n}] {msg}"
                if args.show_formula:
                    out += f"\n  {formula}\n  .inputs: {' '.join(ins)}\n  .outputs: {' '.join(outs)}"
                print(out)
            return msg, None
        elif r < 1.0:
            counts["slower"] += 1
            extra = f" ({tag} {1.0/r:.2f}x slower)"
            extra += " [saved]" if outcome.saved else ""
            msg = f"nah oblig={to0} normal={tn0}{extra}"
            if args.no_live and args.show_slow:
                print(f"[{i}/{args.n}] {msg}")
            return msg, None
        else:
            counts["ties"] += 1
            msg = f"tie oblig={to0} normal={tn0}"
            if args.no_live:
                print(f"[{i}/{args.n}] {msg}")
            return msg, None

    if args.no_live:
        print(f"Binary: {args.binary}")
        print(f"Atoms: {atoms}")
        print(f"Instances: {args.n}")
        print(f"Timeout per run: {args.timeout}s")
        print(f"Comparing -g {args.g}: normal(off) vs obligation(on)")
        print(f"Rerun threshold: {args.rerun_threshold}x; save dir: {args.save_dir}\n")

        for i in range(1, args.n + 1):
            msg, code = handle_one(i)
            if code is not None:
                return code

        print("\n" + "=" * 60)
        comp = counts["comparable"]
        faster = counts["faster"]
        pfast = (faster / comp * 100.0) if comp > 0 else 0.0
        print(f"ok={comp}/{args.n}  YAY={counts['faster']}  nah={counts['slower']}  tie={counts['ties']}"
              f"  timeout={counts['timeouts']}  saved={counts['saved']}")
        print(f"P(YAY|ok)={pfast:.1f}%")
        return 0

    # curses mode
    def _curses_main(stdscr):
        curses.curs_set(0)
        stdscr.nodelay(True)

        # header printed once (outside curses) is annoying; keep it in UI
        state = {
            "i": 1,
            "n": args.n,
            "formula": "",
            "ins": [],
            "outs": [],
            "last_msg": "",
            "counts": counts,
        }

        for i in range(1, args.n + 1):
            # allow quit
            ch = stdscr.getch()
            if ch in (ord('q'), ord('Q')):
                state["last_msg"] = "quit"
                draw_ui(stdscr, state)
                return 0

            # generate instance data for display before run
            formula = random_obligation_formula(args.bool_depth, args.inner_depth, atoms)
            ins, outs = random_partition(atoms)
            state["i"] = i
            state["formula"] = formula
            state["ins"] = ins
            state["outs"] = outs
            draw_ui(stdscr, state)

            # run measurement (uses the already generated formula/partition)
            outcome = maybe_repeat_and_save(args, i, formula, ins, outs, saver)

            # error on first pair => print & exit
            if outcome.had_error and (outcome.pairs[0].normal.status == "error" or outcome.pairs[0].oblig.status == "error"):
                pr = outcome.pairs[0]
                # leave curses to print readable details
                return (3, formula, ins, outs, pr)

            # timeouts on first pair => continue
            if outcome.had_timeout and not outcome.comparable:
                pr = outcome.pairs[0]
                counts["timeouts"] += 1
                tn = fmt_duration(pr.normal.elapsed_s, timeout_s=args.timeout, status=pr.normal.status)
                to = fmt_duration(pr.oblig.elapsed_s, timeout_s=args.timeout, status=pr.oblig.status)
                state["last_msg"] = f"TIMEOUT normal={tn} oblig={to}"
                draw_ui(stdscr, state)
                continue

            # comparable
            counts["comparable"] += 1
            pr0 = outcome.pairs[0]

            # disagreement => exit
            if args.require_same_answer:
                if (pr0.normal.realizable is not None) and (pr0.oblig.realizable is not None) and (pr0.normal.realizable != pr0.oblig.realizable):
                    return (2, formula, ins, outs, pr0)

            if outcome.saved:
                counts["saved"] += 1

            # repeats had timeout/error => continue
            if outcome.repeated and (outcome.had_timeout or outcome.had_error):
                pr_last = outcome.pairs[-1]
                tn = fmt_duration(pr_last.normal.elapsed_s, timeout_s=args.timeout, status=pr_last.normal.status)
                to = fmt_duration(pr_last.oblig.elapsed_s, timeout_s=args.timeout, status=pr_last.oblig.status)
                if pr_last.normal.status == "timeout" or pr_last.oblig.status == "timeout":
                    counts["timeouts"] += 1
                    state["last_msg"] = f"TIMEOUT (reruns) normal={tn} oblig={to}"
                else:
                    state["last_msg"] = f"ERROR (reruns) normal={pr_last.normal.status} oblig={pr_last.oblig.status}"
                draw_ui(stdscr, state)
                continue

            # Decide based on median ratio if repeated else first ratio
            if outcome.repeated and outcome.median_ratio is not None:
                r = outcome.median_ratio
                tag = "median"
            else:
                r = ratio_from_times(pr0.normal.elapsed_s, pr0.oblig.elapsed_s)
                tag = "run1"

            tn0 = fmt_duration(pr0.normal.elapsed_s)
            to0 = fmt_duration(pr0.oblig.elapsed_s)

            if r > 1.0:
                counts["faster"] += 1
                state["last_msg"] = f"YAY oblig={to0} normal={tn0} ({tag} {r:.2f}x)" + (" [saved]" if outcome.saved else "")
            elif r < 1.0:
                counts["slower"] += 1
                state["last_msg"] = f"nah oblig={to0} normal={tn0} ({tag} {1.0/r:.2f}x slower)" + (" [saved]" if outcome.saved else "")
            else:
                counts["ties"] += 1
                state["last_msg"] = f"tie oblig={to0} normal={tn0}"

            draw_ui(stdscr, state)

        return 0

    try:
        res = curses.wrapper(_curses_main)
        if isinstance(res, tuple):
            # exit details from curses mode
            code, formula, ins, outs, pr = res
            print(f"EXIT {code}: disagreement/error")
            print("Formula:", formula)
            print(".inputs:", " ".join(ins))
            print(".outputs:", " ".join(outs))
            print("normal status/time:", pr.normal.status, fmt_duration(pr.normal.elapsed_s, timeout_s=args.timeout, status=pr.normal.status))
            print("oblig  status/time:", pr.oblig.status,  fmt_duration(pr.oblig.elapsed_s, timeout_s=args.timeout, status=pr.oblig.status))
            if code == 2:
                print(f"normal realizable={pr.normal.realizable} oblig realizable={pr.oblig.realizable}")
            print("\n--- normal output (truncated) ---")
            print(pr.normal.output[:args.truncate])
            print("\n--- oblig output (truncated) ---")
            print(pr.oblig.output[:args.truncate])
            return code
        return int(res)
    except KeyboardInterrupt:
        print("\naborted")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
