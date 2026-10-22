#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs the container benchmark as a designed experiment.

  1) Pilot: in the heaviest cell, measure each arm's bytes per row and pick the row count N
     at which the heaviest arm uses --budget-gb.
  2) Factorial: 2^4 full factorial (16 cells) x arms x repetitions. The same repetition number
     means the same seed and so the same model: the arms are compared pairwise.
  3) Scale ladder: two reference cells at N/16, N/8, N/4, N/2 (N itself comes from step 2).
  4) Every run in a seeded random order, each in a fresh process.
  5) Results are appended to the CSV one line at a time; running again skips finished runs.

Every run goes to its own folder under results/ (ignored by git), named after what it measured:
the date, the commit - with -dirty-<hash of the diff> for uncommitted changes - and the budget
(or N, when --N sets it), e.g. results/2026-10-23_6a4c0a5_3.5gb/, beside this script wherever it
is run from. Running the same thing again that day resumes it; --out picks the CSV path instead.
The meta file next to the CSV records the commit as well.

Usage:
  ./build.sh
  python3 run_doe.py --budget-gb 3.5 --reps 3        # full experiment
  python3 run_doe.py --budget-gb 0.15 --reps 2       # quick trial
  python3 make_report.py results/<run>/result.csv    # writes report.xlsx beside it
"""

import argparse
import csv
import hashlib
import itertools
import json
import os
import platform
import random
import subprocess
import sys
import time

# Defaults are relative to this script, not to where it is run from.
HERE = os.path.dirname(os.path.abspath(__file__))

IMPLS = ["dreamor", "map", "flat"]
FACTORS = {
    "W": ["prefix", "mixed"],
    "O": ["shared", "owned"],
    "D": ["dense", "sparse"],
    "S": ["uniform", "skewed"],
}
# Cells the scale ladder runs in
REFERENCE_CELLS = {
    "dense-prefix": dict(W="prefix", O="shared", D="dense", S="uniform"),
    "sparse-mixed": dict(W="mixed", O="owned", D="sparse", S="uniform"),
}
LADDER_FRACTIONS = [1 / 16, 1 / 8, 1 / 4, 1 / 2]
PILOT_N = 300_000

BENCH_COLUMNS = [
    "impl",
    "W",
    "O",
    "D",
    "S",
    "N_target",
    "N",
    "rep",
    "seed",
    "k1_count",
    "build_ms",
    "prep_ms",
    "g1_ms",
    "g2_ms",
    "g3_ms",
    "traverse_ms",
    "hit_ns",
    "miss_ns",
    "container_bytes",
    "peak_rss_bytes",
    "groups",
    "lookups",
    "check_groups",
    "check_traverse",
    "sink",
]
COLUMNS = BENCH_COLUMNS + ["phase", "order", "status", "wall_s"]


def meminfo():
    info = {}
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                info[k] = int(v.split()[0]) * 1024
    except OSError:
        pass
    return info


def cpu_model():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def last_level_cache():
    """Size in bytes of the CPU's last-level cache, from /sys."""
    best_level, best_size = 0, 0
    base = "/sys/devices/system/cpu/cpu0/cache"
    try:
        for d in os.listdir(base):
            if not d.startswith("index"):
                continue
            level = int(open(os.path.join(base, d, "level")).read())
            text = open(os.path.join(base, d, "size")).read().strip()
            mult = {"K": 1024, "M": 1024**2}.get(text[-1], 1)
            size = int(text.rstrip("KM")) * mult
            if level > best_level or (level == best_level and size > best_size):
                best_level, best_size = level, size
    except (OSError, ValueError):
        pass
    return best_size or None


def compiler_version(cxx):
    try:
        return subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    except Exception:
        return "unknown"


def code_state():
    """(commit, dirty, label) of the repository the benchmark builds from, or Nones without git.

    The label names the code exactly: the short commit, plus a hash of the diff when tracked files
    have uncommitted changes, so two different uncommitted states never share a results folder.
    """
    root = os.path.dirname(os.path.abspath(__file__))

    def git(*cmd):
        return subprocess.run(["git", "-C", root, *cmd], capture_output=True, check=True).stdout

    try:
        commit = git("rev-parse", "HEAD").decode().strip()
        diff = git("diff", "HEAD")
    except (OSError, subprocess.CalledProcessError):
        return None, None, "nogit"
    label = commit[:7]
    if diff:
        label += "-dirty-" + hashlib.sha1(diff).hexdigest()[:6]
    return commit, bool(diff), label


def run_one(bench, cfg, timeout):
    cmd = [
        bench,
        "--impl",
        cfg["impl"],
        "--N",
        str(cfg["N_target"]),
        "--rep",
        str(cfg["rep"]),
        "--seed",
        str(cfg["seed"]),
    ]
    for k in FACTORS:
        cmd += [f"--{k}", cfg[k]]
    t0 = time.monotonic()
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        wall = time.monotonic() - t0
        if out.returncode != 0 or not out.stdout.strip():
            return None, "error", wall, out.stderr.strip()[:200]
        values = out.stdout.strip().splitlines()[-1].split(",")
        return dict(zip(BENCH_COLUMNS, values)), "ok", wall, ""
    except subprocess.TimeoutExpired:
        return None, "timeout", time.monotonic() - t0, ""


def key_of(row):
    return tuple(str(row[k]) for k in ["impl", *FACTORS, "N_target", "rep"])


def round2(n):
    """Round down to two significant digits."""
    p = 10 ** (len(str(int(n))) - 2)
    return max(p, int(n) // p * p)


def calibrate(bench, budget_bytes, timeout, seed, impls):
    heavy = dict(W="mixed", O="owned", S="skewed")
    per_row, peak_per_row, sec_per_row = {}, {}, {}
    for impl in impls:
        for d in FACTORS["D"]:
            cfg = dict(heavy, D=d, impl=impl, N_target=PILOT_N, rep=0, seed=seed)
            res, status, wall, err = run_one(bench, cfg, timeout)
            if status != "ok":
                sys.exit(f"pilot failed: {impl} D={d}: {status} {err}")
            n = int(res["N"])
            per_row[impl] = max(per_row.get(impl, 0), float(res["container_bytes"]) / n)
            peak_per_row[impl] = max(peak_per_row.get(impl, 0), float(res["peak_rss_bytes"]) / n)
            sec_per_row[impl] = max(sec_per_row.get(impl, 0), wall / n)
            print(f"  pilot {impl:8s} D={d:6s}: {per_row[impl]:7.1f} B/row (container), {wall:5.1f}s for {n} rows")
    return (
        round2(budget_bytes / max(per_row.values())),
        per_row,
        peak_per_row,
        sec_per_row,
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", default=os.path.join(HERE, "bench"), help="default: the binary beside this script")
    ap.add_argument("--out", help="CSV path; default results/<date>_<commit>_<budget>gb/result.csv")
    ap.add_argument(
        "--budget-gb",
        type=float,
        default=3.5,
        help="memory the heaviest arm may use (GB); sets the row count N",
    )
    ap.add_argument("--N", type=int, default=0, help="set N directly (skips the pilot)")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--seed", type=int, default=20261019)
    ap.add_argument("--timeout", type=int, default=1800, help="limit per run (s)")
    ap.add_argument(
        "--impls",
        default=",".join(IMPLS),
        help="comma-separated; dreamor is the baseline and always included",
    )
    ap.add_argument("--no-ladder", action="store_true")
    ap.add_argument("--no-factorial", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="print the plan only")
    args = ap.parse_args()

    commit, dirty, label = code_state()
    if not args.out:
        size = f"N{args.N}" if args.N else f"{args.budget_gb:g}gb"
        run = f"{time.strftime('%Y-%m-%d')}_{label}_{size}"
        args.out = os.path.join(HERE, "results", run, "result.csv")
    print(f"results: {os.path.relpath(args.out)}")

    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    impls = [c for c in IMPLS if c in args.impls.split(",")]
    if "dreamor" not in impls or len(impls) < 2:
        sys.exit("--impls needs dreamor and at least one other arm.")
    if not os.path.isfile(args.bench):
        sys.exit(f"no bench binary at {args.bench}: run bench/build.sh first.")
    mem = meminfo()
    budget = int(args.budget_gb * 1024**3)
    meta_path = os.path.splitext(args.out)[0] + "_meta.json"

    if args.N:
        n = args.N
        per_row, peak_per_row, sec_per_row = {}, {}, {}
        print(f"N = {n} (given)")
    elif os.path.exists(meta_path):
        with open(meta_path) as f:
            meta = json.load(f)
        n = meta["N"]
        per_row, peak_per_row, sec_per_row = (
            meta["pilot_bytes_per_row"],
            meta["pilot_peak_per_row"],
            meta["pilot_sec_per_row"],
        )
        print(f"N = {n} (resuming from {meta_path})")
    else:
        print(f"pilot (N={PILOT_N}, heaviest cell):")
        n, per_row, peak_per_row, sec_per_row = calibrate(args.bench, budget, args.timeout, args.seed, impls)
        print(f"=> N = {n:,} rows (heaviest arm about {args.budget_gb} GB)")

    if peak_per_row and mem.get("MemAvailable"):
        peak_est = max(peak_per_row.values()) * n
        if peak_est > 0.85 * mem["MemAvailable"]:
            sys.exit(
                f"expected peak resident memory {peak_est / 1024**3:.1f} GB exceeds 85% of the available "
                f"{mem['MemAvailable'] / 1024**3:.1f} GB. Lower --budget-gb."
            )

    meta = dict(
        N=n,
        budget_gb=args.budget_gb,
        reps=args.reps,
        seed=args.seed,
        f2=50,
        f3=100,
        impls=impls,
        design="2^4 full factorial",
        ladder_fractions=LADDER_FRACTIONS,
        reference_cells=REFERENCE_CELLS,
        pilot_bytes_per_row=per_row,
        pilot_peak_per_row=peak_per_row,
        pilot_sec_per_row=sec_per_row,
        cpu=cpu_model(),
        mem_total_bytes=mem.get("MemTotal"),
        llc_bytes=last_level_cache(),
        kernel=platform.release(),
        compiler=compiler_version(os.environ.get("CXX", "g++")),
        python=platform.python_version(),
        commit=commit,
        dirty=dirty,
        started=time.strftime("%Y-%m-%d %H:%M:%S"),
    )
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)

    plan = []
    for rep in range(args.reps):
        seed = args.seed + rep
        if not args.no_factorial:
            for levels in itertools.product(*FACTORS.values()):
                cell = dict(zip(FACTORS, levels))
                for impl in impls:
                    plan.append(
                        dict(
                            cell,
                            impl=impl,
                            N_target=n,
                            rep=rep,
                            seed=seed,
                            phase="factorial",
                        )
                    )
        if not args.no_ladder:
            for cell in REFERENCE_CELLS.values():
                fracs = list(LADDER_FRACTIONS)
                if args.no_factorial:
                    fracs.append(1.0)  # the factorial normally supplies the top rung
                for frac in fracs:
                    for impl in impls:
                        plan.append(
                            dict(
                                cell,
                                impl=impl,
                                N_target=max(1000, int(n * frac)),
                                rep=rep,
                                seed=seed,
                                phase="ladder",
                            )
                        )
    random.Random(args.seed).shuffle(plan)

    done = set()
    if os.path.exists(args.out):
        with open(args.out) as f:
            for row in csv.DictReader(f):
                if row["status"] in ("ok", "timeout"):
                    done.add(key_of(row))
    todo = [p for p in plan if key_of(p) not in done]

    if sec_per_row:
        eta = sum(sec_per_row.get(p["impl"], max(sec_per_row.values())) * p["N_target"] for p in todo)
        print(
            f"{len(todo)} runs to go (of {len(plan)}), at most about {eta / 60:.0f} min "
            f"(the pilot ran in the heaviest cell, so most runs are faster)"
        )
    else:
        print(f"{len(todo)} runs to go (of {len(plan)})")
    if args.dry_run:
        return

    new_file = not os.path.exists(args.out)
    with open(args.out, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        if new_file:
            w.writeheader()
        t_start = time.monotonic()
        for i, cfg in enumerate(todo):
            res, status, wall, err = run_one(args.bench, cfg, args.timeout)
            row = {k: "" for k in COLUMNS}
            row.update({k: cfg[k] for k in ["impl", *FACTORS, "N_target", "rep", "seed"]})
            if res:
                row.update(res)
            row.update(
                phase=cfg["phase"],
                order=len(done) + i,
                status=status,
                wall_s=f"{wall:.2f}",
            )
            w.writerow(row)
            f.flush()
            elapsed = time.monotonic() - t_start
            cell = "/".join(cfg[k] for k in FACTORS)
            print(
                f"[{i + 1}/{len(todo)}] {cfg['phase']:9s} {cfg['impl']:8s} {cell} N={cfg['N_target']:,} "
                f"rep={cfg['rep']} -> {status} {wall:.1f}s  (elapsed {elapsed / 60:.1f} min)"
                f"{'  ' + err if err else ''}",
                flush=True,
            )
    print(f"done: {args.out}   report: python3 make_report.py {args.out}")


if __name__ == "__main__":
    main()
