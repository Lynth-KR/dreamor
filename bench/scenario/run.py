#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Runs a layer-2 scenario (docs/bench/scenario/): a model built end to end, by dreamor and by hand.

The script runs every backend that build.sh built, out of stub, cplex, mathopt and highs, at every size and in every
arm. Each run is a fresh process, and the runs come in a seeded random order. Every repetition runs under a memory
layout of its own (Mytkowicz et al., ASPLOS 2009; van Kempen & Berger, ISMM '26): an environment padding of its own
length and a heap seed of its own. Every repetition also runs under a code layout of its own, on the copies that
LAYOUTS=<reps> ./build.sh links for it with ../common/layout.py. Every arm of a repetition gets the same seeds, so the
arms stay paired. ASLR stays on. --layout off runs every repetition alike, on the plain binaries of ./build.sh.

The results go to ../results/<date>_<commit>_scenario_<scenario>_<size><levels>/runs.jsonl, one line a run, with
meta.json beside it. A run's wall_s is the whole process as the runner saw it, including the time to load the
solver's libraries. Running the same thing again resumes it.

--reuse <earlier run> copies the earlier run's runs of every arm whose binaries are the same, and runs the other arms;
../common/reuse.py says how. --reuse-arms names the arms to copy instead. A tenth of the copied runs, at least 30, run
again as controls of the machine.

Usage:
  LAYOUTS=5 ./build.sh
  python3 run.py --scenario pmedian                # L from 5000 to 50000, 5 repetitions
  python3 run.py --scenario lotsizing              # 170 and 1700 items, about 1e5 and 1e6 rows
  python3 run.py --scenario pmedian --levels 5000 --reps 1 --layout off   # a quick trial
  python3 run.py --scenario pmedian --reuse ../results/<an earlier run of the same plan>
  python3 report.py ../results/<run>
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
    virtualization,
)
from common.reuse import copies  # noqa: E402

BACKENDS = ["stub", "cplex", "mathopt", "highs"]
# Each scenario names its arms, the factor that sets its size, that factor's levels, the arguments that every run
# takes, and the largest size at which an arm still finishes in reasonable time. Lot sizing also names its data's seed.
SCENARIOS = {
    "pmedian": dict(
        impls=["dreamor", "hand", "calls", "callable", "callable-rows"],
        size="L",
        # The sizes are the literature's: 100 customers, 100 locations to open, and 5000 to 50000 candidate locations.
        levels=["5000", "10000", "20000", "50000"],
        fixed=lambda a: dict(C=a.customers, P=a.open),
        largest={},
    ),
    "lotsizing": dict(
        impls=["dreamor", "flat", "plain", "callable", "callable-rows"],
        size="items",
        # With 20 machines that each route about 60% of the items, over 50 periods, an item has about 600 rows.
        levels=["170", "1700"],
        fixed=lambda a: dict(machines=a.machines, periods=a.periods),
        # Every repetition uses the example's own data, whose seed is 7, so the repetitions differ only in their
        # layouts.
        data_seed=7,
        # The plain arm's stock balance scans every route for each item and period, so its time grows with the square
        # of the items.
        largest={"plain": 1700},
    ),
}


def layout_of(seed, rep):
    """Returns the environment padding in bytes and the heap seed of a repetition, drawn as the catalog draws them."""
    g = random.Random(seed * 1_000_003 + rep)
    return g.randrange(4096), g.randrange(1, 2**63)


def key_of(run, spec, fixed):
    return tuple(str(run[k]) for k in ["backend", "impl", spec["size"], *fixed, "rep"])


# The meta fields that an earlier run must share with this run, for --reuse to take the earlier run's runs.
SAME = [
    "scenario",
    "backends",
    "impls",
    "size",
    "levels",
    "fixed",
    "reps",
    "seed",
    "data_seed",
    "solvers",
    "layout",
    "pmu",
    "cpu",
    "pinned_cpus",
    "compiler",
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", required=True, choices=sorted(SCENARIOS))
    ap.add_argument("--backends", help="comma-separated; default: every one build.sh built")
    ap.add_argument("--impls", help="comma-separated arms; default: every arm of the scenario")
    ap.add_argument("--levels", help="sizes, comma-separated; default: the scenario's")
    ap.add_argument("--customers", type=int, default=100, help="pmedian: C")
    ap.add_argument("--open", type=int, default=100, help="pmedian: P, the locations to open")
    ap.add_argument("--machines", type=int, default=20, help="lotsizing")
    ap.add_argument("--periods", type=int, default=50, help="lotsizing")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--seed", type=int, default=20261025)
    ap.add_argument("--pmu", default="auto", help="hardware counters, as ../catalog --pmu takes them; auto or none")
    ap.add_argument("--cpu", default="auto", help="auto, none, or a CPU list")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument(
        "--layout", default="on", choices=["on", "off"], help="a memory and code layout of its own per repetition"
    )
    ap.add_argument("--reuse", help="an earlier run of the same plan whose unchanged arms to copy (../common/reuse.py)")
    ap.add_argument(
        "--reuse-arms", help="comma-separated arms to copy with --reuse; default: those whose binaries are the same"
    )
    ap.add_argument("--out", help="results folder")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.reuse_arms and not args.reuse:
        sys.exit("--reuse-arms names the arms that --reuse copies; it needs --reuse.")
    spec = SCENARIOS[args.scenario]
    fixed = spec["fixed"](args)

    # The dreamor arm runs on build/<scenario>_<backend>, and the hand-written arms on build/<scenario>_<backend>_hand.
    # The hand-written binary is built without the library, so one build of it serves every version of dreamor.
    def binary(backend, impl):
        return os.path.join(HERE, "build", f"{args.scenario}_{backend}" + ("" if impl == "dreamor" else "_hand"))

    built = [b for b in BACKENDS if all(os.path.isfile(binary(b, i)) for i in ("dreamor", "hand"))]
    backends = args.backends.split(",") if args.backends else built
    for b in backends:
        if b not in built:
            sys.exit(
                f"no {args.scenario}_{b} and {args.scenario}_{b}_hand: run build.sh first (it skips a backend "
                "that is not installed)."
            )
    impls = args.impls.split(",") if args.impls else spec["impls"]
    code_layouts = (
        check_layouts(sorted({binary(b, i) for b in backends for i in impls}), args.reps, args.seed)
        if args.layout == "on"
        else None
    )
    levels = [str(int(float(n))) for n in (args.levels or ",".join(spec["levels"])).split(",")]

    commit, dirty, label = code_state()
    out = args.out or os.path.join(
        os.path.dirname(HERE),
        "results",
        f"{time.strftime('%Y-%m-%d')}_{label}_scenario_{args.scenario}_{spec['size']}{'-'.join(levels)}",
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
        scenario=args.scenario,
        backends=backends,
        impls=impls,
        size=spec["size"],
        levels=levels,
        fixed=fixed,
        reps=args.reps,
        seed=args.seed,
        data_seed=spec.get("data_seed"),
        solvers=dict(
            cplex=os.environ.get("CPLEX_STUDIO_DIR"),
            ortools=os.environ.get("ORTOOLS_DIR"),
            highs=os.environ.get("HIGHS_DIR") or os.environ.get("ORTOOLS_DIR"),
        ),
        layout=args.layout,
        code_layouts=code_layouts,
        aslr=read_text("/proc/sys/kernel/randomize_va_space"),
        pmu=pmu,
        pmu_choice=pmu_choice,
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

    plan = [
        dict(backend=b, impl=impl, **{spec["size"]: n}, **fixed, rep=rep, seed=spec.get("data_seed", args.seed + rep))
        for rep, b, n, impl in itertools.product(range(args.reps), backends, levels, impls)
        if int(n) <= spec["largest"].get(impl, int(n))
    ]
    random.Random(args.seed).shuffle(plan)

    def done_key(run):
        """Returns the run's key, and whether the run is a control. A control repeats a copied run under its key."""
        return key_of(run, spec, fixed), bool(run.get("control"))

    reused = []
    if args.reuse:
        binaries = {i: sorted(os.path.basename(binary(b, i)) for b in backends) for i in impls}
        arms = args.reuse_arms.split(",") if args.reuse_arms else None
        reused, controls, meta["reuse"] = copies(
            args.reuse, meta, SAME, code_layouts, binaries, plan, lambda r: key_of(r, spec, fixed), args.seed, arms
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
            exe = binary(p["backend"], p["impl"])
            cmd = [
                laid_out(exe, p["rep"]) if args.layout == "on" else exe,
                "--impl",
                p["impl"],
                f"--{spec['size']}",
                p[spec["size"]],
            ]
            cmd += [x for k, v in fixed.items() for x in (f"--{k}", str(v))]
            cmd += ["--rep", str(p["rep"]), "--seed", str(p["seed"])]
            pad, heap_seed = layout_of(args.seed, p["rep"]) if args.layout == "on" else (0, 0)
            cmd += ["--heap-seed", str(heap_seed)]
            if pmu:
                cmd += ["--pmu", pmu]
            env = dict(os.environ, SCENARIO_PAD="x" * pad)
            start = time.monotonic()
            try:
                res = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout, preexec_fn=pin, env=env)
                status = "ok" if res.returncode == 0 else "error"
                row = json.loads(res.stdout) if status == "ok" else dict(p, error=res.stderr.strip()[:200])
            except subprocess.TimeoutExpired:
                status, row = "timeout", dict(p)
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
