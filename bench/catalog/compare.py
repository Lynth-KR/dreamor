#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Compares the builds of a catalog run made with run.py --builds: prints Markdown, and writes compare.md beside the run.

Each run of one build has its pair in the other, under the same cell, repetition, seed and layout. A ratio is the
later build's time over the earlier build's, in the order in which --builds named them, so a ratio below 1 means that
the later build is faster. For each phase and N, the report gives the geometric mean ratio over the cells, each cell
the mean ln ratio of its pairs, with a 95% interval from the spread of the cells. It also gives how many cells moved
past 5% either way, and the cells that moved most. It lists the results whose checks differ between the builds,
because a change of speed compares like with like only if the results agree.

A phase's time includes what the phases before it left in the heap, so a phase whose code did not change can still
move. Measure such a phase alone, with run.py --phases, before reading its change as an effect.

Usage: python3 compare.py ../results/<run>
"""

import io
import math
import os
import statistics
import sys
from collections import defaultdict

from report import T95, TOTAL, load

MOVED = 0.05  # A cell counts as moved if its ratio is past 1 +- MOVED.


def geomean(cells):
    """Returns the geometric mean ratio over the cells, each cell the mean of its repetitions' ln ratios, or nan if
    there is no cell."""
    xs = [statistics.fmean(v) for v in cells.values()]
    return math.exp(statistics.fmean(xs)) if xs else math.nan


def main():
    folder = sys.argv[1]
    meta, runs = load(folder)
    builds = list(meta.get("builds") or {})
    if len(builds) != 2:
        sys.exit("compare.py needs a run of two builds (run.py --builds a=...,b=...).")
    before, after = builds
    factors = list(meta["two_level"])
    out = io.StringIO()

    def p(line=""):
        out.write(line + "\n")

    pairs = defaultdict(dict)
    for r in runs:
        if r.get("status") == "ok":
            cell = (r["impl"], int(r["k"]), int(r["N_target"]), *(r[f] for f in factors))
            pairs[(cell, r["rep"])][r["build"]] = r
    pairs = {key: d for key, d in pairs.items() if len(d) == 2}

    p(f"# Catalog builds: {meta['family']}, {after} over {before}")
    p()
    p(f"{len(pairs)} pairs; commit {meta['commit']}{' (dirty)' if meta['dirty'] else ''}; {meta['cpu']}")
    p()

    differ = []
    ratios = defaultdict(lambda: defaultdict(list))  # (phase, N) to each cell's ln ratios.
    # With run.py --pmu, the same ratios for each counter. Instructions barely move with the cache or the heap. So a
    # time ratio that the instructions do not follow comes from memory, and one that they follow comes from the code
    # that the compiler made.
    counted = defaultdict(lambda: defaultdict(list))  # (phase, N, counter) to each cell's ln ratios.
    counters = []
    for (cell, rep), d in pairs.items():
        a, b = d[before], d[after]
        for ph, chk in a.get("checks", {}).items():
            if b.get("checks", {}).get(ph) != chk:
                differ.append((cell, rep, ph))
        phases = [ph for ph in a["phases"] if ph in b["phases"]]
        totals = [sum(x["phases"][ph]["ms"] for ph in phases if ph != "build") for x in (a, b)]
        for ph in [*phases, TOTAL]:
            ta, tb = totals if ph == TOTAL else (a["phases"][ph]["ms"], b["phases"][ph]["ms"])
            if ta > 0 and tb > 0:
                ratios[(ph, cell[2])][cell].append(math.log(tb / ta))
        for name in a["phases"][phases[0]].get("counters", {}) if phases else []:
            if name not in counters:
                counters.append(name)
            totals = [sum(x["phases"][ph]["counters"][name] for ph in phases if ph != "build") for x in (a, b)]
            for ph in [*phases, TOTAL]:
                ca, cb = totals if ph == TOTAL else (x["phases"][ph]["counters"][name] for x in (a, b))
                if ca > 0 and cb > 0:
                    counted[(ph, cell[2], name)][cell].append(math.log(cb / ca))

    if differ:
        p(f"## Results that differ ({len(differ)})")
        p()
        for cell, rep, ph in sorted(differ)[:20]:
            p(f"- {ph}, rep {rep}: {cell}")
        p()

    p("## By phase")
    p()
    p(f"| phase | N | ratio | 95% interval | cells faster | cells slower | of |{''.join(f' {c} |' for c in counters)}")
    p(f"|---|---|---|---|---|---|---|{'---|' * len(counters)}")
    order = list(dict.fromkeys(ph for ph, _ in ratios))
    worst = []
    for ph in order:
        for n in sorted({n for q, n in ratios if q == ph}):
            cells = {c: statistics.fmean(xs) for c, xs in ratios[(ph, n)].items()}
            ms = list(cells.values())
            m = statistics.fmean(ms)
            h = T95[min(len(ms) - 1, len(T95) - 1)] * statistics.stdev(ms) / math.sqrt(len(ms)) if len(ms) > 1 else 0
            fast = sum(x < math.log(1 - MOVED) for x in ms)
            slow = sum(x > math.log(1 + MOVED) for x in ms)
            p(
                f"| {ph} | {n:.0e} | {math.exp(m):.3f} | {math.exp(m - h):.3f}-{math.exp(m + h):.3f} | {fast} | "
                f"{slow} | {len(ms)} |" + "".join(f" {geomean(counted[(ph, n, c)]):.3f} |" for c in counters)
            )
            worst += [(x, ph, c) for c, x in cells.items()]
    p()

    # A cost that grows with the columns shows only in this table, because the table above pools the columns.
    ks = sorted({c[1] for (ph, n), cells in ratios.items() for c in cells})
    if len(ks) > 1:
        instructions = "instructions" in counters
        p("## By phase and k")
        p()
        p(
            "The ratio at each k, over N and the cells"
            + (" (in brackets, the instructions')." if instructions else ".")
            + " A phase a k does not have is left empty."
        )
        p()
        p(f"| phase |{''.join(f' k={k} |' for k in ks)}")
        p(f"|---|{'---|' * len(ks)}")
        for ph in order:

            def at_k(table, k):
                cells = [
                    statistics.fmean(xs)
                    for (q, _n, *rest), by in table.items()
                    if q == ph
                    for c, xs in by.items()
                    if c[1] == k
                ]
                return math.exp(statistics.fmean(cells)) if cells else None

            row = []
            for k in ks:
                t = at_k({(q, n): by for (q, n), by in ratios.items()}, k)
                i = (
                    at_k({(q, n, c): by for (q, n, c), by in counted.items() if c == "instructions"}, k)
                    if instructions
                    else None
                )
                row.append("" if t is None else f"{t:.3f}" + (f" ({i:.3f})" if i is not None else ""))
            p(f"| {ph} |{''.join(f' {x} |' for x in row)}")
        p()

    p("## Cells that moved most")
    p()
    p(f"| phase | ratio | {' | '.join(['impl', 'k', 'N', *factors])} |")
    p(f"|---|---|{'---|' * (3 + len(factors))}")
    worst.sort(key=lambda t: -abs(t[0]))
    for x, ph, c in worst[:15]:
        p(f"| {ph} | {math.exp(x):.3f} | {' | '.join(map(str, c))} |")

    text = out.getvalue()
    print(text, end="")
    with open(os.path.join(folder, "compare.md"), "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
