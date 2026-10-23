#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs the solver probe (README.md here): not the benchmark, a look behind it afterwards.

Every probe built by build.sh, both arms, each bound mode and pad, in a seeded random order,
each run a fresh process. Writes probe.csv and prints, per backend, bound mode and pad:

  dreamor ms, flat ms      medians of the build (keys + every family + index)
  flat / dreamor           above 1, dreamor is faster
  container share          the same build against the stub / against this backend, per arm:
                           how much of a real build the containers are

A ratio that moves with --pads, or between machines, is the backend's, not the containers'.

Usage:
  ./build.sh
  python3 run.py                          # 1e6 rows, 3 families, 5 repetitions: a few minutes
  python3 run.py --n 3000000 --pads 0,32,128
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
from run_doe import code_state, cpu_model, short_n  # noqa: E402

PROBES = ["stub", "concert", "mathopt"]  # probe_<name>, as build.sh names them
ARMS = ["dreamor", "flat"]
BOUNDS = ["unique", "few"]
PADS = [0, 32, 128, 512]  # the pads probe.cpp has


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=1_000_000, help="rows")
    ap.add_argument("--families", type=int, default=3)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--bounds", default="unique,few")
    ap.add_argument("--pads", default="0,32", help="register-only instructions per bound, from 0, 32, 128, 512")
    ap.add_argument("--seed", type=int, default=20261024)
    ap.add_argument("--out", help="CSV path (default: ../results/<date>_<commit>_solver_N<n>/probe.csv)")
    args = ap.parse_args()

    probes = [p for p in PROBES if os.path.isfile(os.path.join(HERE, f"probe_{p}"))]
    if "stub" not in probes:
        sys.exit("no probe_stub: run bench/solver/build.sh first.")
    bounds, pads = args.bounds.split(","), [int(p) for p in args.pads.split(",")]
    if not set(bounds) <= set(BOUNDS) or not set(pads) <= set(PADS):
        sys.exit(f"--bounds takes {','.join(BOUNDS)}; --pads takes {','.join(map(str, PADS))}.")

    _, _, label = code_state()
    if not args.out:
        run = f"{time.strftime('%Y-%m-%d')}_{label}_solver_N{short_n(args.n)}"
        args.out = os.path.join(os.path.dirname(HERE), "results", run, "probe.csv")
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    print(f"results: {os.path.relpath(args.out)}  ({cpu_model()}; backends: {', '.join(probes)})")

    plan = [
        (p, a, b, pad, rep) for p in probes for a in ARMS for b in bounds for pad in pads for rep in range(args.reps)
    ]
    random.Random(args.seed).shuffle(plan)
    columns = subprocess.run(
        [os.path.join(HERE, "probe_stub"), "--columns"], capture_output=True, text=True, check=True
    ).stdout.strip()
    rows = []
    with open(args.out, "w", newline="") as f:
        f.write(columns + ",rep\n")
        for i, (p, a, b, pad, rep) in enumerate(plan, 1):
            cmd = [os.path.join(HERE, f"probe_{p}"), a, str(args.n), str(args.families), b, str(pad)]
            line = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip()
            f.write(f"{line},{rep}\n")
            f.flush()
            rows.append(dict(zip(columns.split(","), line.split(","))))
            print(f"\r{i}/{len(plan)}", end="", flush=True)
    print()

    def median(p, a, b, pad):
        xs = [
            float(r["total_ms"]) for r in rows if (r["backend"], r["arm"], r["bounds"], r["pad"]) == (p, a, b, str(pad))
        ]
        return statistics.median(xs) if xs else None

    print(
        f"{'backend':9}{'bounds':8}{'pad':>5}{'dreamor ms':>12}{'flat ms':>10}{'flat/dreamor':>14}{'share dr':>10}"
        f"{'share flat':>12}"
    )
    for p in probes:
        for b in bounds:
            for pad in pads:
                d, fl = median(p, "dreamor", b, pad), median(p, "flat", b, pad)
                sd, sf = median("stub", "dreamor", b, pad), median("stub", "flat", b, pad)
                share = "" if p == "stub" else f"{sd / d:>10.0%}{sf / fl:>12.0%}"
                print(f"{p:9}{b:8}{pad:>5}{d:>12.1f}{fl:>10.1f}{fl / d:>14.2f}{share}")


if __name__ == "__main__":
    main()
