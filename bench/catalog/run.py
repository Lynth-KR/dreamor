#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs one family of the catalog (README.md) as a designed experiment.

The six two-level factors (O, D, S, G, L, I) run as a fraction: resolution IV by default, 16 cells
(L = O*D*S, I = D*S*G), for screening; --resolution 6 runs the half fraction I = O*D*S*G*L, 32
cells, where every main effect and two-factor interaction is clear of the others. k (columns) and
N are crossed with it in full. Every arm runs every cell, each run a fresh process of ./catalog, in
a seeded random order; the same repetition number means the same seed, so the arms are compared
pairwise and a repetition is a block. The data family has entities, not rows: its own two-level
factors (the key's type and the order of access) run in full, without k.

Every repetition runs under its own memory layout (Mytkowicz et al., ASPLOS 2009; van Kempen &
Berger, ISMM '26): an environment padding of its own length moves the stack, and a heap seed of its
own preconditions the heap with holes before the run builds anything. Every arm of a repetition gets
the same layout, so the arms stay paired; ASLR stays on. --layout off runs every repetition alike.

Results go to ../results/<date>_<commit>_catalog_<family>_N<levels>_res<4|6>/runs.jsonl, one line a
run, with meta.json beside it. Running the same thing again resumes it.

--builds compares builds of ./catalog, say of the library before and after a change: every run of
the plan runs once per build, the builds one right after the other in a random order, under the
same seed and layout, so each run of one build has its pair in the other. compare.py reads it.

Usage:
  ./build.sh
  python3 run.py --family lookup --reps 2                       # N = 1e5, 1e6
  python3 run.py --family lookup --n-levels 100000 --reps 1     # quick trial
  python3 report.py ../results/<run>
  python3 run.py --family setop --impls dreamor --reps 3 --builds base=./catalog_base,head=./catalog
  python3 compare.py ../results/<run>
"""

import argparse
import itertools
import json
import os
import platform
import random
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from common.machine import (  # noqa: E402
    choose_cpu,
    code_state,
    compiler_version,
    cpu_model,
    environment,
    meminfo,
    parse_cpulist,
    read_text,
    short_n,
    virtualization,
)

FAMILIES = {
    "lookup": ["dreamor", "identity", "mixed", "array", "map"],
    "group": ["dreamor", "identity", "mixed", "sort", "map"],
    "setop": ["dreamor", "identity", "mixed", "merge", "set"],
    "slice": ["dreamor", "identity", "mixed", "sort", "map"],
    "make": ["dreamor", "identity", "mixed", "sort", "map"],
    "order": ["dreamor", "identity", "mixed", "array", "map"],
    "data": ["dreamor", "umap", "map"],
}
TWO_LEVEL = {  # factor -> (low, high), in the order the generators name them
    "O": ("shared", "owned"),
    "D": ("dense", "sparse"),
    "S": ("uniform", "skewed"),
    "G": ("large", "small"),
    "L": ("ordered", "shuffled"),
    "I": ("aligned", "permuted"),
}
GENERATORS = {  # resolution -> {generated factor: the factors whose product sets it}
    4: {"L": ["O", "D", "S"], "I": ["D", "S", "G"]},  # I = ODSL = DSGI = OGLI: words of length 4
    6: {"I": ["O", "D", "S", "G", "L"]},  # I = ODSGLI
}
# A family whose data are not the rows has two-level factors of its own, run in full, and no k.
OWN_FACTORS = {"data": {"key": ("int", "string"), "access": ("inserted", "random")}}
K_LEVELS = ["2", "3", "5"]
N_LEVELS = ["100000", "1000000"]


def fraction(resolution):
    """The two-level cells of the fraction: base factors in full, generated ones by their products."""
    gen = GENERATORS[resolution]
    base = [f for f in TWO_LEVEL if f not in gen]
    cells = []
    for signs in itertools.product([-1, 1], repeat=len(base)):
        s = dict(zip(base, signs))
        for f, of in gen.items():
            s[f] = 1
            for g in of:
                s[f] *= s[g]
        cells.append({f: TWO_LEVEL[f][0 if s[f] < 0 else 1] for f in TWO_LEVEL})
    return cells


def layout_of(seed, rep):
    """(environment padding in bytes, heap seed) of a repetition: Mytkowicz et al. varied the
    environment over 0-4095 bytes."""
    g = random.Random(seed * 1_000_003 + rep)
    return g.randrange(4096), g.randrange(1, 2**63)


def full(two_level):
    """Every combination of the levels."""
    return [dict(zip(two_level, levels)) for levels in itertools.product(*two_level.values())]


def key_of(run):
    own = OWN_FACTORS.get(run["family"])
    factors = list(own) if own else ["k", *TWO_LEVEL]
    return tuple(str(run.get(k, "")) for k in ["family", "impl", "N_target", *factors, "rep", "build"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--family", required=True, choices=sorted(FAMILIES))
    ap.add_argument("--impls", help="comma-separated arms; default: every arm of the family")
    ap.add_argument("--n-levels", default=",".join(N_LEVELS))
    ap.add_argument("--k-levels", default=",".join(K_LEVELS))
    ap.add_argument("--resolution", type=int, default=4, choices=sorted(GENERATORS))
    ap.add_argument("--reps", type=int, default=2)
    ap.add_argument("--top-reps", type=int, help="repetitions at the largest N; default: --reps")
    ap.add_argument("--seed", type=int, default=20261025)
    ap.add_argument("--lookups", type=int, default=1_000_000)
    ap.add_argument("--pmu", default="", help="hardware counters, as ../bench --pmu takes them")
    ap.add_argument("--phases", default="", help="measure these phases only (the ones they stand on run unreported)")
    ap.add_argument("--cpu", default="auto", help="auto, none, or a CPU list")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--layout", default="on", choices=["on", "off"], help="a memory layout of its own per repetition")
    ap.add_argument("--builds", help="name=path,name=path: builds of ./catalog to compare run by run (compare.py)")
    ap.add_argument("--out", help="results folder")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    builds = (
        dict(b.split("=", 1) for b in args.builds.split(",")) if args.builds else {"": os.path.join(HERE, "catalog")}
    )
    for name, path in builds.items():
        if not os.path.isfile(path):
            sys.exit(f"no {path}: run build.sh first." if not name else f"no {path} for build {name}.")
    impls = args.impls.split(",") if args.impls else FAMILIES[args.family]
    n_levels = [str(int(float(n))) for n in args.n_levels.split(",")]
    own = OWN_FACTORS.get(args.family)
    two_level = own or TWO_LEVEL
    cells = full(own) if own else fraction(args.resolution)
    k_levels = [] if own else args.k_levels.split(",")
    top_n, top_reps = max(n_levels, key=int), args.reps if args.top_reps is None else args.top_reps

    commit, dirty, label = code_state()
    out = args.out or os.path.join(
        os.path.dirname(HERE),
        "results",
        f"{time.strftime('%Y-%m-%d')}_{label}_catalog_{args.family}_N{'-'.join(short_n(int(n)) for n in n_levels)}"
        f"{'_full' if own else f'_res{args.resolution}'}{'_' + '-vs-'.join(builds) if args.builds else ''}",
    )
    os.makedirs(out, exist_ok=True)
    runs_path = os.path.join(out, "runs.jsonl")

    if args.cpu == "auto":
        cpu, cpu_info = choose_cpu()
        cpus = {cpu} if cpu is not None else None
    elif args.cpu == "none":
        cpus, cpu_info = None, {"reason": "not pinned (--cpu none)", "virtualized": virtualization()}
    else:
        cpus = set(parse_cpulist(args.cpu))
        cpu_info = {"reason": f"given (--cpu {args.cpu})", "virtualized": virtualization()}

    plan = []
    for rep in range(args.reps):
        for n, k, cell, impl in itertools.product(n_levels, k_levels or [None], cells, impls):
            if n == top_n and rep >= top_reps:
                continue
            run = dict(family=args.family, impl=impl, N_target=n, **cell, rep=rep, seed=args.seed + rep)
            plan.append(run if k is None else dict(run, k=k))
    random.Random(args.seed).shuffle(plan)
    order = random.Random(args.seed + 1)
    plan = [dict(p, build=b) for p in plan for b in order.sample(list(builds), len(builds))]
    done = set()
    if os.path.exists(runs_path):
        with open(runs_path) as f:
            done = {key_of(json.loads(line)) for line in f if line.strip()}
    todo = [p for p in plan if key_of(p) not in done]
    print(f"results: {os.path.relpath(out)}  ({cpu_model()}; cpu: {sorted(cpus) if cpus else cpu_info['reason']})")
    print(f"{len(todo)} runs to go (of {len(plan)})")
    if args.dry_run:
        return

    meta = dict(
        family=args.family,
        impls=impls,
        resolution="full" if own else args.resolution,
        generators={} if own else GENERATORS[args.resolution],
        two_level=two_level,
        k_levels=k_levels,
        n_levels=n_levels,
        reps=args.reps,
        top_reps=top_reps,
        seed=args.seed,
        layout=args.layout,
        aslr=read_text("/proc/sys/kernel/randomize_va_space"),
        lookups=args.lookups,
        pmu=args.pmu,
        phases=args.phases,
        builds={name: os.path.abspath(path) for name, path in builds.items()} if args.builds else None,
        cpu=cpu_model(),
        pinned_cpus=sorted(cpus) if cpus else None,
        cpu_choice=cpu_info,
        mem_total_bytes=meminfo().get("MemTotal"),
        kernel=platform.release(),
        compiler=compiler_version(os.environ.get("CXX", "g++")),
        commit=commit,
        dirty=dirty,
        started=time.strftime("%Y-%m-%d %H:%M:%S"),
        **environment(cpu=min(cpus) if cpus else None),
    )
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)

    def pin():
        if cpus:
            os.sched_setaffinity(0, cpus)

    t0 = time.monotonic()
    with open(runs_path, "a") as f:
        for i, p in enumerate(todo, 1):
            cmd = [
                builds[p["build"]],
                "--family",
                p["family"],
                "--impl",
                p["impl"],
                "--N",
                p["N_target"],
            ]
            cmd += ["--k", p["k"]] if "k" in p else []
            cmd += [x for f_ in two_level for x in (f"--{f_}", p[f_])]
            cmd += ["--rep", str(p["rep"]), "--seed", str(p["seed"]), "--lookups", str(args.lookups)]
            pad, heap_seed = layout_of(args.seed, p["rep"]) if args.layout == "on" else (0, 0)
            cmd += ["--heap-seed", str(heap_seed)]
            env = dict(os.environ, CATALOG_PAD="x" * pad)
            if args.pmu:
                cmd += ["--pmu", args.pmu]
            if args.phases:
                cmd += ["--phases", args.phases]
            start = time.monotonic()
            try:
                res = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout, preexec_fn=pin, env=env)
                status = "ok" if res.returncode == 0 else "error"
                row = json.loads(res.stdout) if status == "ok" else dict(p, error=res.stderr.strip()[:200])
            except subprocess.TimeoutExpired:
                status, row = "timeout", dict(p)
            if args.builds:
                row["build"] = p["build"]
            row.update(status=status, order=len(done) + i - 1, wall_s=round(time.monotonic() - start, 3), env_pad=pad)
            f.write(json.dumps(row) + "\n")
            f.flush()
            if status != "ok":
                print(f"  {status}: {' '.join(cmd[1:])}", flush=True)
            if i == len(todo) or i % max(1, len(todo) // 10) == 0:
                print(f"{i}/{len(todo)} runs ({(time.monotonic() - t0) / 60:.1f} min)", flush=True)
    print(f"done: {runs_path}   report: python3 report.py {os.path.relpath(out)}")


if __name__ == "__main__":
    main()
