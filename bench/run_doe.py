#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs the container benchmark as a designed experiment (bench/README.md).

  1) Factorial: N (3 levels) x O x D x S x A (3 levels) x G x F, dreamor and flat, x repetitions.
     By default a resolution V half fraction, F = O*D*S*G: 144 cells; --full runs all 288. The
     same repetition number means the same seed and so the same model: the arms are compared
     pairwise, and a repetition is a block.
  2) With --map: the map arm in the reference cell at every N (the stage's hand-written code). It
     does not change with the library, so it is for a published run, not a check.
  3) Optional: --stress runs the reference cell at 3e7 rows (dreamor, flat), outside the
     analysis. A real solver is no part of the experiment: bench/solver/ looks at it afterwards.
  4) Every run in a seeded random order, each in a fresh process, pinned to one CPU of the
     fastest core class (--cpu auto) unless the machine is virtualized.
  5) Results are appended to the CSV one line at a time; running again skips finished runs.

Every run goes to its own folder under results/ (ignored by git), named after what it measured:
the date, the commit - with -dirty-<hash of the diff> for uncommitted changes - and the N
levels, e.g. results/2026-10-23_6a4c0a5_N1e5-1e7/, beside this script wherever it is run from.
Running the same thing again that day resumes it; --out picks the CSV path instead. The meta
file next to the CSV records the commit and the machine.

Usage:
  ./build.sh
  python3 run_doe.py --reps 2 --map                        # the experiment
  python3 run_doe.py --reps 2 --n-levels 100000            # quick trial: N = 1e5 only
  python3 make_report.py results/<run>/result.csv          # writes report.xlsx beside it
"""

import argparse
import csv
import hashlib
import itertools
import json
import os
import platform
import random
import re
import subprocess
import sys
import time

# Defaults are relative to this script, not to where it is run from.
HERE = os.path.dirname(os.path.abspath(__file__))

FACTORIAL_IMPLS = ["dreamor", "flat"]  # map runs in the reference cell only, with --map
FACTORS = {  # CSV column -> levels; N_target is passed as --N
    "N_target": ["100000", "1000000", "10000000"],
    "O": ["shared", "owned"],
    "D": ["dense", "sparse"],
    "S": ["uniform", "skewed"],
    "A": ["aligned", "inner", "all"],
    "G": ["large", "small"],
    "F": ["1", "3"],
}
# The design is a half fraction of the 2-level factors unless --full: F = O*D*S*G, the defining
# relation I = ODSGF, resolution V. Every main effect and two-factor interaction is aliased only
# with interactions of three or more factors, so all of them can be estimated. N and A, the
# 3-level factors, are crossed in full: 3 x 16 x 3 = 144 cells instead of 288.
GENERATED, GENERATORS = "F", ["O", "D", "S", "G"]
# The stage of examples/production_planning.hpp: shared items, every route kept, ids as read.
REFERENCE_CELL = dict(O="shared", D="dense", S="uniform", A="aligned", G="large", F="3")
STRESS_N = "30000000"
# The heaviest cell per row, at two Ns: a line through them separates what a run costs per row
# from what it costs anyway (process start-up, the heap probe) - for the memory check and the ETA.
PILOT_CELL = dict(O="owned", D="sparse", S="skewed", A="all", G="small", F="3")
PILOT_NS = [200_000, 1_000_000]
RUN_COLUMNS = ["phase", "order", "status", "wall_s"]


def flag(factor):
    return "--N" if factor == "N_target" else f"--{factor}"


# ------------------------------------------------------------------------------------- machine
def read_text(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def read_int(path):
    text = read_text(path)
    try:
        return int(text) if text is not None else None
    except ValueError:
        return None


def parse_cpulist(text):
    """'0-3,8,10-11' -> [0, 1, 2, 3, 8, 10, 11]."""
    cpus = []
    for part in (text or "").split(","):
        part = part.strip()
        if not part:
            continue
        lo, _, hi = part.partition("-")
        cpus.extend(range(int(lo), int(hi or lo) + 1))
    return cpus


def virtualization(root="/"):
    """Why pinning would not pin a physical core here, or None on bare metal."""
    release = read_text(os.path.join(root, "proc/sys/kernel/osrelease")) or ""
    if "microsoft" in release.lower():
        return "WSL"
    for line in (read_text(os.path.join(root, "proc/cpuinfo")) or "").splitlines():
        if line.startswith("flags") and "hypervisor" in line.split(":", 1)[-1].split():
            return "virtual machine (hypervisor flag)"
    return None


def cpu_dir(root, cpu):
    return os.path.join(root, "sys/devices/system/cpu", f"cpu{cpu}")


def online_cpus(root="/"):
    base = os.path.join(root, "sys/devices/system/cpu")
    cpus = parse_cpulist(read_text(os.path.join(base, "online")))
    if cpus:
        return cpus
    try:
        return sorted(int(d[3:]) for d in os.listdir(base) if re.fullmatch(r"cpu\d+", d))
    except OSError:
        return list(range(os.cpu_count() or 1))


def last_level_cache(root="/", cpu=0):
    """Size in bytes of the last-level cache that CPU sees (its CCD's L3 on a split part)."""
    best_level, best_size = 0, 0
    base = os.path.join(cpu_dir(root, cpu), "cache")
    try:
        entries = os.listdir(base)
    except OSError:
        return None
    for d in entries:
        if not d.startswith("index"):
            continue
        level = read_int(os.path.join(base, d, "level"))
        text = read_text(os.path.join(base, d, "size"))
        if level is None or not text:
            continue
        mult = {"K": 1024, "M": 1024**2, "G": 1024**3}.get(text[-1], 1)
        size = int(text.rstrip("KMG")) * mult
        if level > best_level or (level == best_level and size > best_size):
            best_level, best_size = level, size
    return best_size or None


def choose_cpu(root="/"):
    """The CPU --cpu auto pins to, or None, and how it was chosen.

    Core type is noise, not a factor, so a run stays on one core class:
      1. Intel hybrid: the P-cores (/sys/devices/cpu_core/cpus);
      2. the largest last-level cache among those (the V-cache CCD of a dual-CCD X3D part);
      3. the highest ACPI CPPC highest_perf, else the highest cpuinfo_max_freq (preferred cores);
      4. the lowest-numbered physical core that is not CPU 0's (CPU 0 takes the interrupts).
    A virtualized machine gets None: pinning there pins a virtual CPU only.
    """
    info = {"virtualized": virtualization(root)}
    pcores = read_text(os.path.join(root, "sys/devices/cpu_core/cpus"))
    info["hybrid"] = pcores is not None
    if info["virtualized"]:
        info["reason"] = f"not pinned: {info['virtualized']} pins a virtual CPU only"
        return None, info
    online = online_cpus(root)
    candidates = [c for c in online if c in set(parse_cpulist(pcores))] if pcores else list(online)
    reasons = ["P-cores (Intel hybrid)" if pcores else "one core type"]

    l3 = {c: last_level_cache(root, c) or 0 for c in candidates}
    if len(set(l3.values())) > 1:
        top = max(l3.values())
        candidates = [c for c in candidates if l3[c] == top]
        reasons.append(f"largest last-level cache ({top >> 20} MB)")
    for name, rel in [
        ("CPPC highest_perf", "acpi_cppc/highest_perf"),
        ("max frequency", "cpufreq/cpuinfo_max_freq"),
    ]:
        perf = {c: read_int(os.path.join(cpu_dir(root, c), rel)) for c in candidates}
        if all(v is not None for v in perf.values()):
            if len(set(perf.values())) > 1:
                top = max(perf.values())
                candidates = [c for c in candidates if perf[c] == top]
                reasons.append(f"highest {name}")
            break  # the first metric the machine has decides

    def core_of(c):
        topo = os.path.join(cpu_dir(root, c), "topology")
        text = read_text(os.path.join(topo, "core_cpus_list")) or read_text(os.path.join(topo, "thread_siblings_list"))
        return tuple(parse_cpulist(text)) or (c,)

    cores = sorted({core_of(c) for c in candidates})
    core = next((k for k in cores if 0 not in k), cores[0])
    cpu = min(c for c in candidates if c in core)
    reasons.append(f"physical core {list(core)}" + (" (not CPU 0's)" if 0 not in core else ""))
    info["reason"] = "; ".join(reasons)
    return cpu, info


def environment(root="/", cpu=None):
    """What the machine was set to, for the meta file: what the bench cannot set itself."""
    c = cpu if cpu is not None else 0
    no_turbo = read_int(os.path.join(root, "sys/devices/system/cpu/intel_pstate/no_turbo"))
    boost = read_int(os.path.join(root, "sys/devices/system/cpu/cpufreq/boost"))
    if boost is None and no_turbo is not None:
        boost = 1 - no_turbo
    return dict(
        governor=read_text(os.path.join(cpu_dir(root, c), "cpufreq/scaling_governor")),
        boost=boost,
        smt_active=read_int(os.path.join(root, "sys/devices/system/cpu/smt/active")),
        thp=read_text(os.path.join(root, "sys/kernel/mm/transparent_hugepage/enabled")),
        llc_bytes=last_level_cache(root, c),
        glibc_tunables=os.environ.get("GLIBC_TUNABLES"),
    )


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


# ------------------------------------------------------------------------------------- runs
def bench_columns(bench):
    out = subprocess.run([bench, "--columns"], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        sys.exit(f"{bench} --columns failed: rebuild it (bench/build.sh).")
    return out.stdout.strip().split(",")


def run_one(bench, columns, cfg, timeout, cpus):
    cmd = [bench, "--impl", cfg["impl"], "--rep", str(cfg["rep"]), "--seed", str(cfg["seed"])]
    for k in FACTORS:
        cmd += [flag(k), str(cfg[k])]
    pin = (lambda: os.sched_setaffinity(0, cpus)) if cpus else None
    t0 = time.monotonic()
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, preexec_fn=pin)
        wall = time.monotonic() - t0
        if out.returncode != 0 or not out.stdout.strip():
            return None, "error", wall, out.stderr.strip()[:200]
        values = out.stdout.strip().splitlines()[-1].split(",")
        return dict(zip(columns, values)), "ok", wall, ""
    except subprocess.TimeoutExpired:
        return None, "timeout", time.monotonic() - t0, ""


def key_of(row):
    return tuple(str(row[k]) for k in ["phase", "impl", *FACTORS, "rep"])


def pilot(bench, columns, timeout, seed, impls, cpus, cell):
    """Per arm, peak bytes and seconds as [fixed, per row], fitted through the cell at PILOT_NS."""
    peak, sec = {}, {}
    for impl in impls:
        points = []
        for n in PILOT_NS:
            cfg = dict(cell, N_target=n, impl=impl, rep=0, seed=seed)
            res, status, wall, err = run_one(bench, columns, cfg, timeout, cpus)
            if status != "ok":
                sys.exit(f"pilot failed: {impl} at {n:,} rows: {status} {err}")
            points.append((int(res["N"]), float(res["peak_rss_bytes"]), wall))
        (n1, p1, t1), (n2, p2, t2) = points
        peak[impl] = [p1 - (p2 - p1) / (n2 - n1) * n1, (p2 - p1) / (n2 - n1)]
        sec[impl] = [max(0.0, t1 - (t2 - t1) / (n2 - n1) * n1), (t2 - t1) / (n2 - n1)]
        print(
            f"  pilot {impl:8s}: {peak[impl][1]:6.1f} B/row + {peak[impl][0] / 1e6:5.0f} MB, "
            f"{sec[impl][1] * 1e6:5.2f} s per 1e6 rows + {sec[impl][0]:4.1f} s"
        )
    return peak, sec


def estimate(model, n):
    return model[0] + model[1] * n


def design_cells(full):
    """The factorial's cells: all of them, or the half fraction F = O*D*S*G."""
    cells = []
    for levels in itertools.product(*FACTORS.values()):
        cell = dict(zip(FACTORS, levels))
        sign = 1
        for f in GENERATORS:
            sign *= 1 if cell[f] == FACTORS[f][1] else -1
        if full or cell[GENERATED] == FACTORS[GENERATED][1 if sign > 0 else 0]:
            cells.append(cell)
    return cells


def short_n(n):
    """10000000 -> '1e7', 30000 -> '3e4'."""
    e = len(str(n)) - 1
    return f"{n // 10**e}e{e}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", default=os.path.join(HERE, "bench"), help="default: the binary beside this script")
    ap.add_argument("--out", help="CSV path; default results/<date>_<commit>_N<levels>/result.csv")
    ap.add_argument("--n-levels", help="comma-separated row counts; default " + ",".join(FACTORS["N_target"]))
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--seed", type=int, default=20261019)
    ap.add_argument("--timeout", type=int, default=1800, help="limit per run (s)")
    ap.add_argument(
        "--map",
        action="store_true",
        help="also run map in the reference cell: the usual hand-written code, for a published run",
    )
    ap.add_argument(
        "--cpu",
        default="auto",
        help="auto: one CPU of the fastest core class (not when virtualized); none; or a CPU list like 2 or 2,3",
    )
    ap.add_argument("--cooldown-s", type=float, default=0, help="pause before each run (s)")
    ap.add_argument("--full", action="store_true", help="all 288 cells instead of the resolution V half fraction")
    ap.add_argument("--stress", action="store_true", help=f"also run the reference cell at {int(STRESS_N):,} rows")
    ap.add_argument("--dry-run", action="store_true", help="print the plan only")
    args = ap.parse_args()

    if args.n_levels:
        FACTORS["N_target"] = [str(int(float(n))) for n in args.n_levels.split(",")]
    n_levels = [int(n) for n in FACTORS["N_target"]]

    commit, dirty, label = code_state()
    if not args.out:
        size = "N" + "-".join(short_n(n) for n in n_levels)
        run = f"{time.strftime('%Y-%m-%d')}_{label}_{size}_{'full' if args.full else 'half'}"
        args.out = os.path.join(HERE, "results", run, "result.csv")
    print(f"results: {os.path.relpath(args.out)}")
    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    impls = FACTORIAL_IMPLS + (["map"] if args.map else [])
    if not os.path.isfile(args.bench):
        sys.exit(f"no bench binary at {args.bench}: run bench/build.sh first.")
    columns = bench_columns(args.bench)

    # CPU: one core class for the whole experiment
    if args.cpu == "auto":
        cpu, cpu_info = choose_cpu()
        cpus = {cpu} if cpu is not None else None
    elif args.cpu == "none":
        cpus, cpu_info = None, {"reason": "not pinned (--cpu none)", "virtualized": virtualization()}
    else:
        cpus = set(parse_cpulist(args.cpu))
        cpu_info = {"reason": f"given (--cpu {args.cpu})", "virtualized": virtualization()}
    env = environment(cpu=min(cpus) if cpus else None)
    print(f"cpu: {sorted(cpus) if cpus else 'not pinned'} - {cpu_info['reason']}")
    if cpu_info.get("virtualized"):
        print(
            f"  warning: {cpu_info['virtualized']}: the host schedules the virtual CPUs, so timings are noisier. "
            "The meta file and the report record it."
        )
    if env["governor"] and env["governor"] != "performance":
        print(f"  warning: CPU governor is '{env['governor']}', not 'performance' (needs root to change).")
    if env["boost"] == 1:
        print("  note: turbo/boost is on - clocks follow temperature; the random run order turns that into noise.")

    mem = meminfo()
    meta_path = os.path.splitext(args.out)[0] + "_meta.json"
    if os.path.exists(meta_path):
        with open(meta_path) as f:
            old = json.load(f)
        peak_model, sec_model = old["pilot_peak_bytes"], old["pilot_seconds"]
        ref_peak, ref_sec = old["pilot_reference_peak_bytes"], old["pilot_reference_seconds"]
        print(f"pilot: reused from {meta_path}")
    else:
        # The heaviest cell bounds memory and time from above; the reference cell, one of the
        # lightest, bounds the time from below and sizes the stress run.
        rows = ", ".join(f"{n:,}" for n in PILOT_NS)
        print(f"pilot (heaviest cell at {rows} rows):")
        peak_model, sec_model = pilot(args.bench, columns, args.timeout, args.seed, FACTORIAL_IMPLS, cpus, PILOT_CELL)
        print(f"pilot (reference cell at {rows} rows):")
        ref_peak, ref_sec = pilot(args.bench, columns, args.timeout, args.seed, FACTORIAL_IMPLS, cpus, REFERENCE_CELL)
    checks = [(max(n_levels), peak_model, "Lower --n-levels")]
    if args.stress:
        checks.append((int(STRESS_N), ref_peak, "Drop --stress"))
    for n, model, fix in checks:
        peak_est = max(estimate(m, n) for m in model.values())
        if mem.get("MemAvailable") and peak_est > 0.85 * mem["MemAvailable"]:
            sys.exit(
                f"expected peak resident memory {peak_est / 1024**3:.1f} GB at {n:,} rows exceeds 85% of the "
                f"available {mem['MemAvailable'] / 1024**3:.1f} GB. {fix}."
            )

    meta = dict(
        design=(
            "N(3) x O x D x S x A(3) x G x F, "
            + ("full factorial" if args.full else "half fraction F = ODSG (resolution V)")
            + ", dreamor vs flat; map in the reference cell"
        ),
        full=args.full,
        factors=FACTORS,
        reference_cell=REFERENCE_CELL,
        reps=args.reps,
        seed=args.seed,
        impls=impls,
        stress_n=int(STRESS_N) if args.stress else None,
        pilot_cell=PILOT_CELL,
        pilot_peak_bytes=peak_model,  # [fixed, per row]
        pilot_seconds=sec_model,  # [fixed, per row]
        pilot_reference_peak_bytes=ref_peak,
        pilot_reference_seconds=ref_sec,
        pinned_cpus=sorted(cpus) if cpus else None,
        cpu_choice=cpu_info,
        cpu=cpu_model(),
        mem_total_bytes=mem.get("MemTotal"),
        kernel=platform.release(),
        compiler=compiler_version(os.environ.get("CXX", "g++")),
        python=platform.python_version(),
        commit=commit,
        dirty=dirty,
        started=time.strftime("%Y-%m-%d %H:%M:%S"),
        **env,
    )
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)

    plan = []
    for rep in range(args.reps):
        seed = args.seed + rep
        for cell in design_cells(args.full):
            for impl in FACTORIAL_IMPLS:
                plan.append(dict(cell, impl=impl, rep=rep, seed=seed, phase="factorial"))
        if args.map:
            for n in FACTORS["N_target"]:
                plan.append(dict(REFERENCE_CELL, N_target=n, impl="map", rep=rep, seed=seed, phase="reference"))
        if args.stress:
            for impl in FACTORIAL_IMPLS:
                plan.append(dict(REFERENCE_CELL, N_target=STRESS_N, impl=impl, rep=rep, seed=seed, phase="stress"))
    random.Random(args.seed).shuffle(plan)

    done = set()
    if os.path.exists(args.out):
        with open(args.out) as f:
            for row in csv.DictReader(f):
                if row["status"] in ("ok", "timeout"):
                    done.add(key_of(row))
    todo = [p for p in plan if key_of(p) not in done]

    def eta(model):
        slowest = max(model.values(), key=lambda m: m[1])
        seconds = sum(estimate(model.get(p["impl"], slowest), int(p["N_target"])) for p in todo)
        return (seconds + args.cooldown_s * len(todo)) / 60

    print(
        f"{len(todo)} runs to go (of {len(plan)}), about {eta(ref_sec):.0f}-{eta(sec_model):.0f} min "
        "(every run at the reference cell's pace - at the heaviest cell's)"
    )
    if args.dry_run:
        return

    new_file = not os.path.exists(args.out)
    with open(args.out, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=columns + RUN_COLUMNS)
        if new_file:
            w.writeheader()
        t_start = time.monotonic()
        for i, cfg in enumerate(todo):
            if args.cooldown_s:
                time.sleep(args.cooldown_s)
            res, status, wall, err = run_one(args.bench, columns, cfg, args.timeout, cpus)
            row = {k: "" for k in columns + RUN_COLUMNS}
            row.update({k: cfg[k] for k in ["impl", *FACTORS, "rep", "seed"]})
            if res:
                row.update(res)
                if cpus and not {int(res["cpu_start"]), int(res["cpu_end"])} <= cpus:
                    err = f"ran on CPU {res['cpu_start']}->{res['cpu_end']}, outside {sorted(cpus)}"
            row.update(phase=cfg["phase"], order=len(done) + i, status=status, wall_s=f"{wall:.2f}")
            w.writerow(row)
            f.flush()
            elapsed = time.monotonic() - t_start
            cell = "/".join(str(cfg[k]) for k in FACTORS)
            print(
                f"[{i + 1}/{len(todo)}] {cfg['phase']:9s} {cfg['impl']:8s} {cell} rep={cfg['rep']} "
                f"-> {status} {wall:.1f}s  (elapsed {elapsed / 60:.1f} min){'  ' + err if err else ''}",
                flush=True,
            )
    print(f"done: {args.out}   report: python3 make_report.py {args.out}")


if __name__ == "__main__":
    main()
