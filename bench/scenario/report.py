#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarizes a scenario run (run.py): prints Markdown and writes report.md beside it.

Per backend, hand-written arm and size: each arm's median time to build the model (every phase before
the solver), dreamor / that arm for the model, to the solver's start (the benchmarks' measure: the
model, extract and the solve with no time; the stub has no solver, so the two are one) and for the
whole process as the runner saw it - the geometric mean of the repetitions' ratios, the arms of a
repetition having run under the same layout, with a 95% interval; below 1, dreamor is faster. Then
each phase's ratio, and every check the arms disagree on: a ratio compares like with like only when
the models are the same. Last, each arm across the backends that solve: its median time to the
solver's start on each, and each backend / MathOpt - the same model, through another API to HiGHS.

Usage: python3 report.py ../results/<run>
"""

import io
import math
import os
import statistics
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from catalog.report import T95, load  # noqa: E402
from common.reuse import split  # noqa: E402

SOLVER = ["extract", "solve0"]


def measures(run):
    phases = run["phases"]
    model = sum(v["ms"] for p, v in phases.items() if p not in SOLVER)
    return {
        "model": model,
        "to_solve": model + sum(phases[p]["ms"] for p in SOLVER if p in phases),
        "process": run["wall_s"] * 1e3,
        **{p: v["ms"] for p, v in phases.items()},
    }


def ratio(lns):
    """Geometric mean of the ratios and its 95% interval, from their logarithms."""
    mean = statistics.fmean(lns)
    if len(lns) < 2:
        return math.exp(mean), None
    half = T95[min(len(lns) - 1, len(T95) - 1)] * statistics.stdev(lns) / math.sqrt(len(lns))
    return math.exp(mean), (math.exp(mean - half), math.exp(mean + half))


def shown(r, ci):
    return f"{r:.3f}" + (f" [{ci[0]:.3f}, {ci[1]:.3f}]" if ci else "")


def main():
    folder = sys.argv[1]
    meta, runs = load(folder)
    size = meta.get("size", "L")
    fixed = meta.get("fixed") or {"C": meta.get("customers"), "P": meta.get("open")}
    runs, drift = split(runs, lambda r: tuple(str(r.get(k, "")) for k in ["backend", "impl", size, *fixed, "rep"]))
    out = io.StringIO()

    def p(line=""):
        out.write(line + "\n")

    by = defaultdict(dict)  # (backend, size, rep) -> impl -> run
    phase_order = []
    for r in runs:
        if r.get("status") == "ok" and not r.get("skipped"):
            by[(r["backend"], int(r[size]), r["rep"])][r["impl"]] = r
            phase_order += [ph for ph in r["phases"] if ph not in phase_order]
    others = [i for i in meta["impls"] if i != "dreamor"]

    p(f"# Scenario: {meta['scenario']}")
    p()
    p(
        ", ".join(f"{k} = {v}" for k, v in fixed.items())
        + f"; commit {meta['commit']}{' (dirty)' if meta['dirty'] else ''}; {meta['cpu']}"
    )
    if drift:
        p()
        p(drift)
    failed = [r for r in runs if r.get("status") != "ok"]
    if failed:
        p()
        p(
            f"{len(failed)} runs failed: "
            + ", ".join(f"{r['backend']} {r['impl']} {size}={r[size]}" for r in failed[:10])
        )

    differ = sorted(
        {
            (b, n, o)
            for (b, n, _), d in by.items()
            for o in others
            if "dreamor" in d and o in d and d["dreamor"].get("checks") != d[o].get("checks")
        }
    )
    p()
    p(
        "Checks: "
        + (
            "every pair agrees."
            if not differ
            else "differ at " + ", ".join(f"{b} {o} {size}={n}" for b, n, o in differ)
        )
    )

    headline = defaultdict(list)  # (backend, arm, measure) -> ln ratio per size
    for b in [b for b in meta["backends"] if any(k[0] == b for k in by)]:
        for o in others:
            pairs = {k: d for k, d in sorted(by.items()) if k[0] == b and "dreamor" in d and o in d}
            if not pairs:
                continue
            sizes = sorted({k[1] for k in pairs})
            p()
            p(f"## {b}: dreamor / {o}")
            p()
            p(
                f"| {size} | rows | dreamor model ms | {o} model ms | model | to solve | process | peak RSS MB (dreamor / {o}) |"
            )
            p("|---|---|---|---|---|---|---|---|")
            for n in sizes:
                reps = [d for k, d in pairs.items() if k[1] == n]
                m = {impl: [measures(d[impl]) for d in reps] for impl in ("dreamor", o)}
                cells = []
                for what in ("model", "to_solve", "process"):
                    lns = [math.log(dm[what] / om[what]) for dm, om in zip(m["dreamor"], m[o])]
                    headline[(b, o, what)].append(statistics.fmean(lns))
                    cells.append(shown(*ratio(lns)))
                rss = {impl: statistics.median(d[impl]["peak_rss_bytes"] for d in reps) / 2**20 for impl in m}
                rows = reps[0]["dreamor"].get("rows") or n * (fixed.get("C") or 1)
                p(
                    f"| {n} | {rows:.1e} | {statistics.median(x['model'] for x in m['dreamor']):.1f} "
                    f"| {statistics.median(x['model'] for x in m[o]):.1f} | {' | '.join(cells)} "
                    f"| {rss['dreamor']:.0f} / {rss[o]:.0f} |"
                )
            p()
            present = [ph for ph in phase_order if any(ph in d["dreamor"]["phases"] for d in pairs.values())]
            # A phase the other arm has nothing for (an empty one: sets by hand) is shown as dreamor's time.
            phases = [
                ph for ph in present if any(d[o]["phases"].get(ph, {}).get("ms", 0) > 0.05 for d in pairs.values())
            ]
            only = [ph for ph in present if ph not in phases]
            p("Phase ratios (geometric mean over repetitions):")
            p()
            p(f"| {size} | " + " | ".join(phases) + "".join(f" | dreamor {ph} ms" for ph in only) + " |")
            p("|---|" + "---|" * (len(phases) + len(only)))
            for n in sizes:
                reps = [d for k, d in pairs.items() if k[1] == n]
                cells = []
                for ph in phases:
                    lns = [
                        math.log(d["dreamor"]["phases"][ph]["ms"] / d[o]["phases"][ph]["ms"])
                        for d in reps
                        if ph in d["dreamor"]["phases"] and d[o]["phases"].get(ph, {}).get("ms", 0) > 0
                    ]
                    cells.append(f"{ratio(lns)[0]:.2f}" if lns else "-")
                for ph in only:
                    cells.append(f"{statistics.median(d['dreamor']['phases'][ph]['ms'] for d in reps):.1f}")
                p(f"| {n} | " + " | ".join(cells) + " |")

    p()
    p("## Headline")
    p()
    p("Geometric mean over the sizes of dreamor / each arm:")
    p()
    p("| backend | arm | model | to solve | process |")
    p("|---|---|---|---|---|")
    for b, o in dict.fromkeys((b, o) for b, o, _ in headline):
        p(
            f"| {b} | {o} | "
            + " | ".join(
                f"{math.exp(statistics.fmean(headline[(b, o, w)])):.3f}" for w in ("model", "to_solve", "process")
            )
            + " |"
        )

    # Each arm against the backend's matrix API - the arrays filled by position and handed over at
    # once: what an expression API costs over it, the vendor's own (Concert) and dreamor's alike.
    matrix = {"cplex": "callable", "highs": "hand"}
    for b, ref in matrix.items():
        arms = [i for i in meta["impls"] if i != ref and any(k[0] == b and i in d and ref in d for k, d in by.items())]
        if not arms:
            continue
        sizes = sorted({k[1] for k, d in by.items() if k[0] == b and ref in d})
        p()
        p(f"## {b}: each arm / {ref}, the matrix API (model, then to the solver's start)")
        p()
        p(f"| {size} | {ref} to start ms | " + " | ".join(arms) + " |")
        p("|---|---|" + "---|" * len(arms))
        for n in sizes:
            reps = [d for k, d in by.items() if k[0] == b and k[1] == n and ref in d]
            cells = []
            for o in arms:
                pairs = [(measures(d[o]), measures(d[ref])) for d in reps if o in d]
                if not pairs:
                    cells.append("-")
                    continue
                model = math.exp(statistics.fmean(math.log(a["model"] / r["model"]) for a, r in pairs))
                start = math.exp(statistics.fmean(math.log(a["to_solve"] / r["to_solve"]) for a, r in pairs))
                cells.append(f"{model:.2f} / {start:.2f}")
            base = statistics.median(measures(d[ref])["to_solve"] for d in reps)
            p(f"| {n} | {base:.1f} | " + " | ".join(cells) + " |")

    # Each arm across the solving backends: the same model, the same repetition's layout.
    solving = [b for b in meta["backends"] if b != "stub" and any(k[0] == b for k in by)]
    if len(solving) > 1:
        base = "mathopt" if "mathopt" in solving else solving[0]
        p()
        p(f"## Across backends: time to the solver's start, and each / {base}")
        for impl in meta["impls"]:
            sizes = sorted({k[1] for k, d in by.items() if k[0] == base and impl in d})
            shown_on = [b for b in solving if any(k[0] == b and impl in d for k, d in by.items())]
            if not sizes or len(shown_on) < 2:
                continue
            p()
            p(f"{impl}:")
            p()
            p(
                f"| {size} | "
                + " | ".join(f"{b} ms" for b in shown_on)
                + "".join(f" | {b} / {base}" for b in shown_on if b != base)
                + " |"
            )
            p("|---|" + "---|" * (2 * len(shown_on) - 1))
            for n in sizes:
                cells = []
                for b in shown_on:
                    t = [measures(d[impl])["to_solve"] for k, d in by.items() if k[0] == b and k[1] == n and impl in d]
                    cells.append(f"{statistics.median(t):.1f}" if t else "-")
                for b in shown_on:
                    if b == base:
                        continue
                    lns = [
                        math.log(measures(d[impl])["to_solve"] / measures(by[(base, n, rep)][impl])["to_solve"])
                        for (bb, nn, rep), d in by.items()
                        if bb == b and nn == n and impl in d and impl in by.get((base, n, rep), {})
                    ]
                    cells.append(shown(*ratio(lns)) if lns else "-")
                p(f"| {n} | " + " | ".join(cells) + " |")

    text = out.getvalue()
    print(text, end="")
    with open(os.path.join(folder, "report.md"), "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
