#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs the visit-order experiment (README.md here) and prints how the planner's order fares.

Every case - a shape, the three sources' sizes, each predicate's selectivity - runs in every
variant (the planner's choice, every forced order, the late filter), repetitions times, each a
fresh process, in a seeded random order. The counting build runs each variant once more for the
number of predicate calls. Writes plan.csv and prints, per case:

  planned       the order the planner picked (A, B, C = declaration order)
  free ms       the planner's choice as written: makeSet(as, bs, cs).where(...)
  best, worst   the fastest and slowest forced order, and their times
  regret        the planner's order forced / the best forced: the same machinery, so only the
                order differs (1 = the planner picked the best)
  vs decl       ABC forced / the planner's order forced: what planning gained over declaration
                order (1 = it kept declaration order, or that was as good)
  late          the late filter / free: what running each predicate early is worth
  chain         the planner's order forced / free: what forcing an order costs by itself
  calls         predicate calls of the planner's choice and of the best order

A join has no choice to make - it is visited as written - so its row sets makeSet<>(S, U)
against makeSet<>(U, S).

Usage:
  ./build.sh
  python3 run.py                 # every case, 5 repetitions: a few minutes
  python3 run.py --preds periodic   # predicates a branch predictor can learn
  python3 run.py --reps 3 --shapes pair,filters
"""

import argparse
import os
import random
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from run_doe import code_state, cpu_model  # noqa: E402

ORDERS = ["ABC", "ACB", "BAC", "BCA", "CAB", "CBA"]
N, BIG = 200, 2000  # a source's rows; a big source's
WEAK, STRONG = 2, 30  # a predicate keeps 1 in this many
SIZES = {"equal": (N, N, N), "A big": (BIG, N, N), "C big": (N, N, BIG)}


def cases(shapes):
    """(shape, sizes label, (nA, nB, nC), (d1, d2, d3), selectivity label)."""
    out = []
    for shape in shapes:
        for size, n in SIZES.items():
            if shape == "join":  # S = A x B, U = B x C: big A makes S big, big C makes U big
                if size == "equal":
                    continue
                sels = {"weak": (WEAK, WEAK, 1), "strong": (STRONG, STRONG, 1)}
            elif shape == "filters":  # q(A), q(C): which of the two keeps less
                sels = {"A keeps less": (STRONG, WEAK, 1), "C keeps less": (WEAK, STRONG, 1)}
            else:
                sels = {"weak": (WEAK,) * 3, "strong": (STRONG,) * 3}
            for sel, d in sels.items():
                out.append((shape, size, n, d, sel))
    return out


def variants(shape):
    return ["SU", "US"] if shape == "join" else ["free", "late"] + ORDERS


def run(binary, shape, n, d, preds, variant):
    cmd = [os.path.join(HERE, binary), shape, *map(str, n), *map(str, d), preds, variant]
    line = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip()
    return line


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--shapes", default="pair,filters,chain,triangle,join")
    ap.add_argument(
        "--preds",
        choices=["random", "periodic"],
        default="random",
        help="periodic: the same selectivities in a pattern a branch predictor can learn",
    )
    ap.add_argument("--seed", type=int, default=20261024)
    ap.add_argument("--out", help="CSV path (default: ../results/<date>_<commit>_plan_<preds>/plan.csv)")
    args = ap.parse_args()

    for b in ("plan", "plan_count"):
        if not os.path.isfile(os.path.join(HERE, b)):
            sys.exit(f"no {b}: run bench/plan/build.sh first.")
    all_cases = cases(args.shapes.split(","))

    _, _, label = code_state()
    if not args.out:
        args.out = os.path.join(
            os.path.dirname(HERE), "results", f"{time.strftime('%Y-%m-%d')}_{label}_plan_{args.preds}", "plan.csv"
        )
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    print(f"results: {os.path.relpath(args.out)}  ({cpu_model()})")

    plan = [(c, v, rep) for c in all_cases for v in variants(c[0]) for rep in range(args.reps)]
    random.Random(args.seed).shuffle(plan)
    columns = subprocess.run(
        [os.path.join(HERE, "plan"), "--columns"], capture_output=True, text=True, check=True
    ).stdout.strip()
    timed, evals = [], {}
    with open(args.out, "w", newline="") as f:
        f.write(columns + ",rep\n")
        for i, (c, v, rep) in enumerate(plan, 1):
            line = run("plan", c[0], c[2], c[3], args.preds, v)
            f.write(f"{line},{rep}\n")
            timed.append((c, v, dict(zip(columns.split(","), line.split(",")))))
            print(f"\r{i}/{len(plan)}", end="", flush=True, file=sys.stderr)
        print(file=sys.stderr)
        for c in all_cases:
            for v in variants(c[0]):
                row = dict(zip(columns.split(","), run("plan_count", c[0], c[2], c[3], args.preds, v).split(",")))
                evals[(c, v)] = int(row["evals"])
                f.write(",".join(row.values()) + ",count\n")

    bad = {(c, r["checksum"]) for c, _, r in timed}
    if len(bad) != len(all_cases):
        sys.exit("checksums differ between variants of a case: the orders did not make the same set.")

    def ms(c, v):
        return statistics.median(float(r["ms"]) for cc, vv, r in timed if (cc, vv) == (c, v))

    print(
        f"{'shape':9}{'sizes':7}{'select':14}{'planned':>8}{'free ms':>9}{'best':>6}{'ms':>8}{'worst':>7}{'ms':>9}"
        f"{'regret':>8}{'vs decl':>8}{'late':>7}{'chain':>7}{'calls free':>12}{'best':>11}"
    )
    for c in all_cases:
        shape, size, _, _, sel = c
        if shape == "join":  # no planner: written as makeSet<>(S, U), against makeSet<>(U, S)
            su, us = ms(c, "SU"), ms(c, "US")
            best, worst = ("SU", "US") if su <= us else ("US", "SU")
            print(
                f"{shape:9}{size:7}{sel:14}{'SU':>8}{su:>9.2f}{best:>6}{min(su, us):>8.2f}{worst:>7}"
                f"{max(su, us):>9.2f}{su / min(su, us):>8.2f}"
            )
            continue
        planned = next(r["planned"] for cc, _, r in timed if cc == c)
        forced = {o: ms(c, o) for o in ORDERS}
        best = min(forced, key=forced.get)
        worst = max(forced, key=forced.get)
        free = ms(c, "free")
        print(
            f"{shape:9}{size:7}{sel:14}{planned:>8}{free:>9.2f}{best:>6}{forced[best]:>8.2f}{worst:>7}"
            f"{forced[worst]:>9.2f}{forced[planned] / forced[best]:>8.2f}{forced['ABC'] / forced[planned]:>8.2f}"
            f"{ms(c, 'late') / free:>7.1f}{forced[planned] / free:>7.2f}{evals[(c, 'free')]:>12,}"
            f"{evals[(c, best)]:>11,}"
        )


if __name__ == "__main__":
    main()
