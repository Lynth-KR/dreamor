#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runs a layer-2 scenario (README.md): a model built end to end, by dreamor and by hand.

Every backend built by build.sh (stub, cplex, mathopt), every size and every arm, each run a fresh
process, in a seeded random order. Every repetition runs under its own memory layout (Mytkowicz et
al., ASPLOS 2009; van Kempen & Berger, ISMM '26): an environment padding of its own length and a
heap seed of its own; every arm of a repetition gets the same layout, so they stay paired. ASLR stays
on. --layout off runs every repetition alike.

Results go to ../results/<date>_<commit>_scenario_<scenario>_<size><levels>/runs.jsonl, one line a run, with
meta.json beside it; wall_s is the whole process as the runner saw it, loading the solver's
libraries included. Running the same thing again resumes it.

Usage:
  ./build.sh
  python3 run.py --scenario pmedian                # L = 5000 to 50000, 5 repetitions
  python3 run.py --scenario lotsizing              # 170 and 1700 items (1e5 and 1e6 rows)
  python3 run.py --scenario pmedian --levels 5000 --reps 1   # quick trial
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

BACKENDS = ["stub", "cplex", "mathopt"]
# Per scenario: its arms, the factor that sets its size and that factor's levels, the arguments
# every run takes, and the largest size an arm still finishes at in reasonable time.
SCENARIOS = {
    "pmedian": dict(
        impls=["dreamor", "hand"],
        size="L",
        # The literature's sizes: 100 customers, 100 locations to open, 5000 to 50000 candidates.
        levels=["5000", "10000", "20000", "50000"],
        fixed=lambda a: dict(C=a.customers, P=a.open),
        largest={},
    ),
    "lotsizing": dict(
        impls=["dreamor", "flat", "plain"],
        size="items",
        # 20 machines, each routing about 60% of the items, over 50 periods: 600 rows an item.
        levels=["170", "1700"],
        fixed=lambda a: dict(machines=a.machines, periods=a.periods),
        # plain's stock balance scans every machine's routes for each (item, period): it grows with
        # the square of the items.
        largest={"plain": 1700},
    ),
}


def layout_of(seed, rep):
    """(environment padding in bytes, heap seed) of a repetition, as the catalog draws them."""
    g = random.Random(seed * 1_000_003 + rep)
    return g.randrange(4096), g.randrange(1, 2**63)


def key_of(run, spec, fixed):
    return tuple(str(run[k]) for k in ["backend", "impl", spec["size"], *fixed, "rep"])


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
    ap.add_argument("--layout", default="on", choices=["on", "off"], help="a memory layout of its own per repetition")
    ap.add_argument("--out", help="results folder")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    spec = SCENARIOS[args.scenario]
    fixed = spec["fixed"](args)
    binary = {b: os.path.join(HERE, f"{args.scenario}_{b}") for b in BACKENDS}
    backends = args.backends.split(",") if args.backends else [b for b in BACKENDS if os.path.isfile(binary[b])]
    for b in backends:
        if not os.path.isfile(binary.get(b, "")):
            sys.exit(f"no {args.scenario}_{b}: run build.sh first (it skips a backend that is not installed).")
    impls = args.impls.split(",") if args.impls else spec["impls"]
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

    plan = [
        dict(backend=b, impl=impl, **{spec["size"]: n}, **fixed, rep=rep, seed=args.seed + rep)
        for rep, b, n, impl in itertools.product(range(args.reps), backends, levels, impls)
        if int(n) <= spec["largest"].get(impl, int(n))
    ]
    random.Random(args.seed).shuffle(plan)
    done = set()
    if os.path.exists(runs_path):
        with open(runs_path) as f:
            done = {key_of(json.loads(line), spec, fixed) for line in f if line.strip()}
    todo = [p for p in plan if key_of(p, spec, fixed) not in done]
    pmu, pmu_choice = choose_pmu(args.pmu)
    print(f"results: {os.path.relpath(out)}  ({cpu_model()}; cpu: {sorted(cpus) if cpus else cpu_info['reason']})")
    print(f"counters: {pmu_choice}")
    print(f"{len(todo)} runs to go (of {len(plan)})")
    if args.dry_run:
        return

    meta = dict(
        scenario=args.scenario,
        backends=backends,
        impls=impls,
        size=spec["size"],
        levels=levels,
        fixed=fixed,
        reps=args.reps,
        seed=args.seed,
        layout=args.layout,
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
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)

    def pin():
        if cpus:
            os.sched_setaffinity(0, cpus)

    t0 = time.monotonic()
    with open(runs_path, "a") as f:
        for i, p in enumerate(todo, 1):
            cmd = [binary[p["backend"]], "--impl", p["impl"], f"--{spec['size']}", p[spec["size"]]]
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
