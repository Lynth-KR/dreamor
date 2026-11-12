#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Runs the visit-order experiment (README.md here) and prints how the planner's order fares.

A case is a shape, the three sources' sizes and each predicate's selectivity. Every case runs in every variant: the
planner's choice, every forced order and the late filter. Each variant runs --reps times, each time in a fresh
process, and the runs come in a seeded random order. The counting build then runs each variant once more, for the
number of predicate calls. The script writes plan.csv and prints these columns for each case:

  planned       the order that the planner picked. A, B, C is the declaration order.
  free ms       the time of the planner's choice as written:
                of<cA, cB, cC>(in<cA>(as), in<cB>(bs), in<cC>(cs)).where(...).
  best, worst   the fastest and the slowest forced order, and their times.
  regret        the planner's order forced over the best order forced. Both use the same machinery, so only the
                order differs. 1 means that the planner picked the best order.
  vs decl       ABC forced over the planner's order forced: what planning gained over declaration order. 1 means
                that the planner kept declaration order, or that declaration order was as good.
  late          the late filter over free: what running each predicate early is worth.
  chain         the planner's order forced over free: what forcing an order costs by itself.
  calls         the predicate calls of the planner's choice and of the best order.

A join has no order to choose, because it is visited as written. So for a join, the script sets variant SU,
of<cA, cB, cC>(in<cA, cB>(sS), in<cB, cC>(sU)), against variant US, of<cB, cC, cA>(in<cB, cC>(sU), in<cA, cB>(sS)).

Usage:
  ./build.sh
  python3 run.py                 # every case, 5 repetitions, in a few minutes
  python3 run.py --preds periodic   # predicates that a branch predictor can learn
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
from common.machine import code_state, cpu_model  # noqa: E402

ORDERS = ["ABC", "ACB", "BAC", "BCA", "CAB", "CBA"]
N, BIG = 200, 2000  # The rows of a source, and the rows of a big source.
WEAK, STRONG = 2, 30  # A predicate keeps 1 row in this many.
SIZES = {"equal": (N, N, N), "A big": (BIG, N, N), "C big": (N, N, BIG)}


def cases(shapes):
    """Returns the cases of `shapes`, each as (shape, sizes label, (nA, nB, nC), (d1, d2, d3), selectivity label)."""
    out = []
    for shape in shapes:
        for size, n in SIZES.items():
            if shape == "join":
                # S is A x B and U is B x C, so a big A makes S big, and a big C makes U big.
                if size == "equal":
                    continue
                sels = {"weak": (WEAK, WEAK, 1), "strong": (STRONG, STRONG, 1)}
            elif shape == "filters":
                # The filters are q(A) and q(C), and the selectivities say which of the two keeps less.
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
        if shape == "join":
            # A join has no planner's order: SU, the join as written, is set against US.
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
