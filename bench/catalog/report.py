#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarizes a catalog run (run.py): prints Markdown and writes summary.md and cells.csv beside it.

A ratio is a hand-written arm's time over dreamor's, so above 1 means dreamor is faster; `best` is
the fastest hand-written arm that ran in the cell. An arm that does not fit a cell (an array over
too many id combinations) does not run there, so the effects on `best` carry where it fits. Ratios are geometric means. An effect is the
difference of mean ln(ratio) between a factor's two levels, shown as the multiplier it puts on the
ratio, with a standard error from the variance between repetitions within cells (a cell run once
takes the variance pooled over the cells run more than once).

Usage: python3 report.py ../results/<run>
"""

import csv
import io
import json
import math
import os
import statistics
import sys

TOTAL = "total"  # every phase but build, summed: what the family's operations cost together
# Two-sided 95% quantiles of Student's t by degrees of freedom, 1 to 30 (index 0 unused).
T95 = [
    0,
    12.706,
    4.303,
    3.182,
    2.776,
    2.571,
    2.447,
    2.365,
    2.306,
    2.262,
    2.228,
    2.201,
    2.179,
    2.160,
    2.145,
    2.131,
    2.120,
    2.110,
    2.101,
    2.093,
    2.086,
    2.080,
    2.074,
    2.069,
    2.064,
    2.060,
    2.056,
    2.052,
    2.048,
    2.045,
    2.042,
]


def load(folder):
    with open(os.path.join(folder, "meta.json")) as f:
        meta = json.load(f)
    with open(os.path.join(folder, "runs.jsonl")) as f:
        runs = [json.loads(line) for line in f if line.strip()]
    return meta, runs


def main():
    folder = sys.argv[1]
    meta, runs = load(folder)
    factors = list(meta["two_level"])
    out = io.StringIO()

    def p(line=""):
        out.write(line + "\n")

    ok = [r for r in runs if r.get("status") == "ok"]
    p(f"# Catalog: {meta['family']}")
    p()
    p(
        f"{meta['cpu']}, commit {meta['commit'][:7]}{' (dirty)' if meta['dirty'] else ''}, resolution "
        f"{meta['resolution']}, k {', '.join(meta['k_levels'])}, N {', '.join(meta['n_levels'])}, "
        f"{meta['reps']} repetitions ({meta['top_reps']} at the largest N)."
    )
    p(
        f"Runs: {len(runs)}, ok {len(ok)}, skipped by an arm {sum(1 for r in ok if r['skipped'])}, "
        f"failed {len(runs) - len(ok)}."
    )

    cell_keys = ["k", "N_target", *factors]
    # Every arm that ran a cell's repetition must agree on every check.
    by_model = {}
    for r in ok:
        if not r["skipped"]:
            by_model.setdefault(tuple(r[c] for c in cell_keys) + (r["rep"],), []).append(r)
    mismatches = sum(
        1 for rs in by_model.values() for c in rs[0]["checks"] if len({r["checks"].get(c) for r in rs}) > 1
    )
    p(f"Check mismatches: {mismatches}")
    p()
    # Coverage is a result: an arm that cannot run a cell (Unsupported) is not a gap to fill.
    all_cells = {tuple(r[c] for c in cell_keys) for r in ok}
    p(f"## Coverage: cells each arm ran, of {len(all_cells)}")
    p()
    p("| arm | cells | unsupported, and why |")
    p("|---|---|---|")
    for arm in meta["impls"]:
        ran_cells = {tuple(r[c] for c in cell_keys) for r in ok if r["impl"] == arm and not r["skipped"]}
        why = {}
        for r in ok:
            if r["impl"] == arm and r["skipped"]:
                why.setdefault(r["skipped"], set()).add(tuple(r[c] for c in cell_keys))
        reasons = "; ".join(f"{reason} ({len(cells)})" for reason, cells in why.items()) or "-"
        p(f"| {arm} | {len(ran_cells)} | {reasons} |")
    lone = sorted(
        {key[:-1] for key, rs in by_model.items() if {r["impl"] for r in rs} == {"dreamor"}},
        key=str,
    )
    if lone:
        p()
        p(
            f"Cells no hand-written arm could run ({len(lone)}): "
            + "; ".join(" ".join(f"{c}={v}" for c, v in zip(cell_keys, cell)) for cell in lone)
        )
    p()

    phases = []  # every phase any run has, in the order runs have them (a shape may exist for some k only)
    for r in ok:
        for ph in r["phases"]:
            if ph != "build" and ph not in phases:
                phases.append(ph)
    phases = ["build", *phases, TOTAL]
    hand = [i for i in meta["impls"] if i != "dreamor"]

    def ms(r, ph):
        if ph == TOTAL:
            return sum(v["ms"] for k, v in r["phases"].items() if k != "build")
        return r["phases"][ph]["ms"] if ph in r["phases"] else 0  # 0: the run has no such phase

    # ln(hand / dreamor) per cell, repetition, phase and comparison ("best" included).
    obs = {}  # (cell, comparison, phase) -> [ln ratio per repetition]
    best_count = {}
    for key, rs in by_model.items():
        cell, arms = key[:-1], {r["impl"]: r for r in rs}
        if "dreamor" not in arms:
            continue
        d = arms["dreamor"]
        for ph in phases:
            ratios = {
                h: math.log(ms(arms[h], ph) / ms(d, ph))
                for h in hand
                if h in arms and ms(d, ph) > 0 and ms(arms[h], ph) > 0  # an empty phase has no ratio
            }
            if not ratios:
                continue
            best = min(ratios, key=ratios.get)
            ratios["best"] = ratios[best]
            for comp, v in ratios.items():
                obs.setdefault((cell, comp, ph), []).append(v)
    comps = [*hand, "best"]
    means = {k: statistics.fmean(v) for k, v in obs.items()}
    for (cell, comp, ph), v in means.items():  # the fastest arm per cell, over its repetitions
        if comp in hand and ph == TOTAL:
            others = [means.get((cell, h, TOTAL)) for h in hand if (cell, h, TOTAL) in means]
            if v == min(others):
                best_count[comp] = best_count.get(comp, 0) + 1

    # Pure error: the variance between repetitions, pooled over the cells run more than once. With
    # --layout on it holds the layout's variation too.
    def pooled(ph, comp):
        num = den = 0.0
        for (cell, c, phh), v in obs.items():
            if c == comp and phh == ph and len(v) > 1:
                num += statistics.variance(v) * (len(v) - 1)
                den += len(v) - 1
        return (num / den if den else float("nan")), den

    def ci(ph, comp):
        """95% interval of the mean ln ratio over the cells, from the pure error alone."""
        cells = [(cell, len(v)) for (cell, c, phh), v in obs.items() if c == comp and phh == ph]
        s2, df = pooled(ph, comp)
        if not cells or not df:
            return None
        se = math.sqrt(sum(s2 / n for _, n in cells)) / len(cells)
        t = T95[min(int(df), 30)] if df < 30 else 1.96
        m = statistics.fmean(means[(cell, comp, ph)] for cell, _ in cells)
        return math.exp(m - t * se), math.exp(m + t * se)

    p("## Absolute times, ms (geometric mean over the runs of each arm, over the cells it ran in)")
    p()
    arms = ["dreamor", *hand]
    for n in meta["n_levels"]:
        p(f"N = {n}")
        p()
        p("| phase | " + " | ".join(arms) + " |")
        p("|---" * (len(arms) + 1) + "|")
        for ph in phases:
            row = []
            for arm in arms:
                xs = [ms(r, ph) for r in ok if not r["skipped"] and r["impl"] == arm and r["N_target"] == int(n)]
                xs = [x for x in xs if x > 0]
                row.append(f"{math.exp(statistics.fmean(math.log(x) for x in xs)):.2f}" if xs else "-")
            p(f"| {ph} | " + " | ".join(row) + " |")
        p()
    # How much each arm moves between repetitions of a cell - with --layout on, between memory
    # layouts: a property of the arm, reported as such rather than folded into an error.
    p("## Variation between repetitions, per arm (with --layout on: between memory layouts)")
    p()
    p("Standard deviation of ln(ms) between the repetitions of a cell, pooled over the cells, as a percentage.")
    p()
    p("| phase | " + " | ".join(arms) + " |")
    p("|---" * (len(arms) + 1) + "|")
    for ph in phases:
        row = []
        for arm in arms:
            reps = {}
            for r in ok:
                if r["skipped"] or r["impl"] != arm or ms(r, ph) <= 0:
                    continue
                reps.setdefault(tuple(r[c] for c in cell_keys), []).append(math.log(ms(r, ph)))
            num = sum(statistics.variance(v) * (len(v) - 1) for v in reps.values() if len(v) > 1)
            den = sum(len(v) - 1 for v in reps.values() if len(v) > 1)
            row.append(f"{100 * math.sqrt(num / den):.1f}" if den else "-")
        p(f"| {ph} | " + " | ".join(row) + " |")
    p()
    p("## Ratios, hand-written / dreamor (geometric mean over cells; above 1, dreamor is faster)")
    p()
    p("95% interval from the variation between repetitions; the cells differ from each other by more, by design.")
    p()
    p("| phase | " + " | ".join(comps) + " |")
    p("|---" * (len(comps) + 1) + "|")
    for ph in phases:
        row = []
        for c in comps:
            vals = [v for (cell, comp, phh), v in means.items() if comp == c and phh == ph]
            interval = ci(ph, c) if vals else None
            text = f"{math.exp(statistics.fmean(vals)):.2f}" if vals else "-"
            if interval:
                text += f" [{interval[0]:.2f}, {interval[1]:.2f}]"
            row.append(f"{text} ({len(vals)})" if vals else "-")
        p(f"| {ph} | " + " | ".join(row) + " |")
    p()
    p("The fastest hand-written arm, over the family's operations together (`total`): ")
    p(", ".join(f"{h} in {n} cells" for h, n in sorted(best_count.items(), key=lambda x: -x[1])))
    p()

    def levels_table(ph, comp):
        cells = {cell: v for (cell, c, phh), v in means.items() if c == comp and phh == ph}
        rows = []
        for i, f in enumerate(cell_keys):
            for lv in sorted({cell[i] for cell in cells}, key=lambda x: (len(str(x)), str(x))):
                vals = [v for cell, v in cells.items() if cell[i] == lv]
                rows.append((f, lv, math.exp(statistics.fmean(vals)), len(vals)))
        return rows

    for comp in ["best", *hand]:
        p(f"## Effects on ln({comp} / dreamor)")
        p()
        p("Two-level factors: multiplier the high level puts on the ratio (standard error of the effect).")
        p()
        p("| phase | " + " | ".join(f"{f} {meta['two_level'][f][1]}" for f in factors) + " |")
        p("|---" * (len(factors) + 1) + "|")
        for ph in phases:
            cells = {cell: v for (cell, c, phh), v in means.items() if c == comp and phh == ph}
            if not cells:
                continue
            s2, _ = pooled(ph, comp)
            row = []
            for f in factors:
                i = cell_keys.index(f)
                hi_lv = meta["two_level"][f][1]
                hi = [v for cell, v in cells.items() if cell[i] == hi_lv]
                lo = [v for cell, v in cells.items() if cell[i] != hi_lv]
                if not hi or not lo:
                    row.append("-")
                    continue
                eff = statistics.fmean(hi) - statistics.fmean(lo)
                reps = {cell: len(obs[(cell, comp, ph)]) for cell in cells}
                var = (
                    sum(s2 / reps[cell] for cell in cells if cell[i] == hi_lv) / len(hi) ** 2
                    + sum(s2 / reps[cell] for cell in cells if cell[i] != hi_lv) / len(lo) ** 2
                )
                row.append(f"{math.exp(eff):.2f} ({math.sqrt(var):.3f})")
            p(f"| {ph} | " + " | ".join(row) + " |")
        p()
        p("k and N: the ratio at each level." if meta["k_levels"] else "N: the ratio at each level.")
        p()
        p(
            "| phase | "
            + " | ".join([*(f"k={k}" for k in meta["k_levels"]), *(f"N={n}" for n in meta["n_levels"])])
            + " |"
        )
        p("|---" * (len(meta["k_levels"]) + len(meta["n_levels"]) + 1) + "|")
        for ph in phases:
            rows = {(f, str(lv)): g for f, lv, g, _ in levels_table(ph, comp)}
            vals = [rows.get(("k", k)) for k in meta["k_levels"]] + [
                rows.get(("N_target", n)) for n in meta["n_levels"]
            ]
            p(f"| {ph} | " + " | ".join(f"{v:.2f}" if v else "-" for v in vals) + " |")
        p()

    text = out.getvalue()
    print(text)
    with open(os.path.join(folder, "summary.md"), "w") as f:
        f.write(text)
    with open(os.path.join(folder, "cells.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([*cell_keys, "comparison", "phase", "ratio", "repetitions"])
        for (cell, comp, ph), v in sorted(obs.items()):
            w.writerow([*cell, comp, ph, f"{math.exp(statistics.fmean(v)):.4f}", len(v)])


if __name__ == "__main__":
    main()
