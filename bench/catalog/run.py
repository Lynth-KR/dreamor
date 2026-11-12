#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Runs one family of the catalog, which docs/bench/catalog/ describes, as a designed experiment.

The six two-level factors O, D, S, G, L and I run as a fraction. By default the fraction has resolution IV and 16
cells, for screening: L = O*D*S and I = D*S*G. --resolution 6 runs the half fraction I = O*D*S*G*L, 32 cells, in
which every main effect and every two-factor interaction is clear of the others. k and N are crossed with the
fraction in full. k is the number of columns: 1, 2, 3, 5 or 8. Only lookup and make run k = 1, and k = 8 runs on
build/catalog8. Every arm runs every cell in a seeded random order, each run in a fresh process of the arm's build:
build/catalog for dreamor, build/catalog_hand for the hand-written arms. A repetition number gives every arm the
same seed, so the arms are compared in pairs, and a repetition is a block. The data family has entities, not rows.
Its own two-level factors, the type of the key and the order of access, run in full, without k.

Every repetition runs under a memory layout of its own (Mytkowicz et al., ASPLOS 2009; van Kempen & Berger,
ISMM '26). An environment padding of its own length moves the stack. A heap seed of its own preconditions the heap
with holes before the run builds anything. A code layout of its own comes from the copies that LAYOUTS=<reps>
./build.sh links for the repetition (../common/layout.py). Every arm of a repetition gets the same seeds, so the arms
stay paired. ASLR stays on. --layout off runs every repetition alike, on the plain binaries of ./build.sh.

Results go to ../results/<date>_<commit>_catalog_<family>_N<levels>_res<4|6>/runs.jsonl, one line for each run, with
meta.json beside it. The folder of the data family ends in _full instead of _res<4|6>. If the same plan runs again,
it resumes.

--builds compares builds of build/catalog, such as the library before and after a change. Every run of the plan runs
once for each build, the builds one right after the other in a random order, under the same seed and layout. So each
run of one build has its pair in the other. compare.py reads the result.

--reuse <earlier run> copies the earlier run's runs of every arm whose binaries are the same, and runs the other arms
(../common/reuse.py): a new version of dreamor against the hand-written arms measured before, or a changed
hand-written arm against dreamor. --reuse-arms names the arms to copy instead. A tenth of the copied runs, at least
30, run again as controls of the machine.

Usage:
  LAYOUTS=5 ./build.sh
  python3 run.py --family lookup --reps 2                       # N = 1e5, 1e6
  python3 run.py --family lookup --n-levels 100000 --reps 1 --layout off   # quick trial
  python3 report.py ../results/<run>
  python3 run.py --family setop --impls dreamor --reps 3 --builds base=./catalog_base,head=build/catalog
  python3 compare.py ../results/<run>
  python3 run.py --family lookup --resolution 6 --reps 5 --reuse ../results/<an earlier run of the same plan>
  python3 run.py --family lookup --resolution 6 --reps 5 --reuse ../results/<...> --reuse-arms dreamor,map
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
from common.layout import check_layouts, laid_out  # noqa: E402
from common.machine import (  # noqa: E402
    choose_cpu,
    choose_pmu,
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
from common.reuse import copies  # noqa: E402

FAMILIES = {
    "lookup": ["dreamor", "identity", "mixed", "array", "map"],
    "group": ["dreamor", "identity", "mixed", "sort", "map"],
    "setop": ["dreamor", "identity", "mixed", "merge", "set"],
    "slice": ["dreamor", "identity", "mixed", "sort", "map"],
    "make": ["dreamor", "identity", "mixed", "sort", "map"],
    "order": ["dreamor", "identity", "mixed", "array", "map"],
    "data": ["dreamor", "umap", "map"],
}
# The (low, high) levels of each factor, in the order in which the generators name the factors.
TWO_LEVEL = {
    "O": ("shared", "owned"),
    "D": ("dense", "sparse"),
    "S": ("uniform", "skewed"),
    "G": ("large", "small"),
    "L": ("ordered", "shuffled"),
    "I": ("aligned", "permuted"),
}
# For each resolution, each generated factor and the factors whose product sets its level.
GENERATORS = {
    4: {"L": ["O", "D", "S"], "I": ["D", "S", "G"]},  # The defining relation is I = ODSL = DSGI = OGLI.
    6: {"I": ["O", "D", "S", "G", "L"]},  # The defining relation is I = ODSGLI.
}
# A family whose data are not the rows has two-level factors of its own, run in full, and no k.
OWN_FACTORS = {"data": {"key": ("int", "string"), "access": ("inserted", "random")}}
K_LEVELS = ["1", "2", "3", "5", "8"]
# At k = 1 a row has no period beside k1, so only the families whose operations need no period run k = 1.
K_SUPPORTED = {"lookup": K_LEVELS, "make": K_LEVELS}
K_WITH_PERIOD = ["2", "3", "5", "8"]
# k = 8 runs on build/catalog8, the build whose rows are 8 wide, as data.hpp describes. The path of a k = 8 build is
# the path of its build followed by "8".
K_WIDE = "8"
# The hand-written arms run on build/catalog_hand, and on build/catalog_hand8 for k = 8, which are built without the
# library. So one build of them serves every build of dreamor that --builds compares.
HAND = os.path.join(HERE, "build", "catalog_hand")


def binary(path, k, impl="dreamor"):
    base = path if impl == "dreamor" else HAND
    return base + "8" if k == K_WIDE else base


N_LEVELS = ["100000", "1000000"]


def fraction(resolution):
    """Returns the two-level cells of the fraction: the base factors in full, and each generated factor by the product
    of its generators."""
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
    """Returns the environment padding in bytes and the heap seed of a repetition.

    The padding is drawn from 0 to 4095 bytes, the range over which Mytkowicz et al. varied the environment."""
    g = random.Random(seed * 1_000_003 + rep)
    return g.randrange(4096), g.randrange(1, 2**63)


def full(two_level):
    """Returns every combination of the levels."""
    return [dict(zip(two_level, levels)) for levels in itertools.product(*two_level.values())]


def key_of(run):
    own = OWN_FACTORS.get(run["family"])
    factors = list(own) if own else ["k", *TWO_LEVEL]
    return tuple(str(run.get(k, "")) for k in ["family", "impl", "N_target", *factors, "rep", "build"])


def done_key(run):
    """Returns the key of a run and whether the run is a control. With --reuse, a control repeats a copied run."""
    return key_of(run), bool(run.get("control"))


# The settings that an earlier run must share with this one for --reuse to take its runs.
SAME = [
    "family",
    "impls",
    "resolution",
    "k_levels",
    "n_levels",
    "reps",
    "top_reps",
    "seed",
    "layout",
    "lookups",
    "pmu",
    "phases",
    "cpu",
    "pinned_cpus",
    "compiler",
]


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
    ap.add_argument("--pmu", default="auto", help="hardware counters, as ./catalog --pmu takes them; auto or none")
    ap.add_argument("--phases", default="", help="measure these phases only (the ones they stand on run unreported)")
    ap.add_argument("--cpu", default="auto", help="auto, none, or a CPU list")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument(
        "--layout", default="on", choices=["on", "off"], help="a memory and code layout of its own per repetition"
    )
    ap.add_argument("--builds", help="name=path,name=path: builds of ./catalog to compare run by run (compare.py)")
    ap.add_argument("--reuse", help="an earlier run of the same plan whose unchanged arms to copy (../common/reuse.py)")
    ap.add_argument(
        "--reuse-arms", help="comma-separated arms to copy with --reuse; default: those whose binaries are the same"
    )
    ap.add_argument("--out", help="results folder")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.reuse and args.builds:
        sys.exit("--reuse measures one build of dreamor; it does not take --builds.")
    if args.reuse_arms and not args.reuse:
        sys.exit("--reuse-arms names the arms that --reuse copies; it needs --reuse.")
    builds = (
        dict(b.split("=", 1) for b in args.builds.split(","))
        if args.builds
        else {"": os.path.join(HERE, "build", "catalog")}
    )
    for name, path in builds.items():
        if not os.path.isfile(path):
            sys.exit(f"no {path}: run build.sh first." if not name else f"no {path} for build {name}.")
    if not os.path.isfile(HAND):
        sys.exit(f"no {HAND}: run build.sh first.")
    impls = args.impls.split(",") if args.impls else FAMILIES[args.family]
    n_levels = [str(int(float(n))) for n in args.n_levels.split(",")]
    own = OWN_FACTORS.get(args.family)
    two_level = own or TWO_LEVEL
    cells = full(own) if own else fraction(args.resolution)
    k_levels = [] if own else [k for k in args.k_levels.split(",") if k in K_SUPPORTED.get(args.family, K_WITH_PERIOD)]
    if K_WIDE in k_levels:
        for name, path in builds.items():
            if not os.path.isfile(binary(path, K_WIDE)):
                sys.exit(f"no {binary(path, K_WIDE)} for k = {K_WIDE}: build.sh builds it beside {path}.")
        if not os.path.isfile(binary(HAND, K_WIDE, "hand")):
            sys.exit(f"no {binary(HAND, K_WIDE, 'hand')} for k = {K_WIDE}: run build.sh.")
    top_n, top_reps = max(n_levels, key=int), args.reps if args.top_reps is None else args.top_reps
    code_layouts = (
        check_layouts(
            sorted({binary(path, k, i) for path in builds.values() for k in k_levels or [None] for i in impls}),
            args.reps,
            args.seed,
        )
        if args.layout == "on"
        else None
    )

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

    pmu, pmu_choice = choose_pmu(args.pmu)
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
        code_layouts=code_layouts,
        aslr=read_text("/proc/sys/kernel/randomize_va_space"),
        lookups=args.lookups,
        pmu=pmu,
        pmu_choice=pmu_choice,
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
    reused = []
    if args.reuse:
        binaries = {i: sorted({os.path.basename(binary(builds[""], k, i)) for k in k_levels or [None]}) for i in impls}
        arms = args.reuse_arms.split(",") if args.reuse_arms else None
        reused, controls, meta["reuse"] = copies(
            args.reuse, meta, SAME, code_layouts, binaries, plan, key_of, args.seed, arms
        )
        plan += controls
        random.Random(args.seed + 3).shuffle(plan)
    done = set()
    if os.path.exists(runs_path):
        with open(runs_path) as f:
            done = {done_key(json.loads(line)) for line in f if line.strip()}
    reused = [r for r in reused if done_key(r) not in done]
    done |= {done_key(r) for r in reused}
    todo = [p for p in plan if done_key(p) not in done]
    print(f"results: {os.path.relpath(out)}  ({cpu_model()}; cpu: {sorted(cpus) if cpus else cpu_info['reason']})")
    print(f"counters: {pmu_choice}")
    print(f"{len(todo)} runs to go (of {len(plan)})")
    if args.dry_run:
        return

    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    if reused:
        with open(runs_path, "a") as f:
            f.writelines(json.dumps(r) + "\n" for r in reused)
        print(f"{len(reused)} runs of {', '.join(meta['reuse']['arms'])} copied from {meta['reuse']['source']}")

    def pin():
        if cpus:
            os.sched_setaffinity(0, cpus)

    t0 = time.monotonic()
    with open(runs_path, "a") as f:
        for i, p in enumerate(todo, 1):
            exe = binary(builds[p["build"]], p.get("k"), p["impl"])
            cmd = [
                laid_out(exe, p["rep"]) if args.layout == "on" else exe,
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
            if pmu:
                cmd += ["--pmu", pmu]
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
            if p.get("control"):
                row["control"] = True
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
