#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Measures only the arms that changed, against the runs of the others that an earlier run made, for run.py --reuse.

build.sh builds the dreamor arm and the hand-written arms into binaries of their own, and the hand-written ones
without the library. So a change to dreamor leaves the hand-written binaries, and each repetition's copy of them, byte
for byte as they were, and a change to a hand-written arm leaves dreamor's. run.py --reuse <earlier run> copies the
earlier run's runs of every arm whose binaries are the ones that run measured, and runs the other arms. When the new
run resumes, it finds the copies done and leaves them. A repetition's memory and code layout follow from the seed, so
a new run and an earlier one of the same repetition stay paired.

The copies are taken only if what made them is what the new run would make:

- the same machine, compiler, seed, repetitions and design;
- a code layout of its own per repetition in both runs, so that each copy's sha256 is recorded;
- every binary of a copied arm, in every repetition, with the sha256 that the earlier run recorded.

run.py --reuse-arms names the arms to copy instead. An arm named there is copied even if its binary changed, on the
word of whoever runs it that the arm's own code did not: the hand-written arms share a binary, so a change to one of
them changes the binary of the others. The record in meta.json names each arm copied that way.

The machine itself may have changed since the earlier run: its kernel, its firmware, its temperature or what else it
runs. So some of the copied runs that measured something run again, as controls: a tenth of them, at least 30, or all
of them if there are fewer. The seed draws the controls across the cells and the repetitions. An arm copied although
its binary changed gets at least 30 controls of its own, for the code around it may have moved it. report.py shows
the controls' time against the copies' time, for all of them and for each arm copied that way. If the 95% interval of
a ratio leaves 1 +- 2%, report.py warns, and the whole run must be made again. The controls stay out of every ratio.
"""

import json
import math
import os
import random
import statistics
import sys

CONTROL_FRACTION = 0.10
MIN_CONTROLS = 30
DRIFT = 0.02


def copies(old, meta, same, code_layouts, binaries, plan, key, seed, arms=None):
    """Returns the runs to copy from the earlier run at `old`, the runs to make again as controls, and the record
    that meta.json keeps of the copy.

    `same` names the meta fields that both runs must share, and `binaries` maps each arm of `plan` to the names of the
    binaries that it runs on. Without `arms`, every arm whose binaries all have the sha256 that the earlier run
    recorded is copied. `arms` names the arms to copy instead, whatever their binaries. The runs to copy are the
    earlier run's rows for those arms, each marked with the run that it was copied from. If the new run cannot take
    them from the earlier run, the function exits and says why.
    """
    with open(os.path.join(old, "meta.json")) as f:
        old_meta = json.load(f)
    for field in same:
        if old_meta.get(field) != meta.get(field):
            sys.exit(f"--reuse {old}: {field} differs ({old_meta.get(field)!r}, now {meta.get(field)!r}); run in full.")
    if not code_layouts or not old_meta.get("code_layouts"):
        sys.exit(f"--reuse {old}: both runs need --layout on, whose copies are checked by their sha256.")

    def unchanged(arm):
        return all(
            old_meta["code_layouts"].get(rep, {}).get("sha256", {}).get(name) == layout["sha256"].get(name)
            for rep, layout in code_layouts.items()
            for name in binaries[arm]
        )

    impls = sorted({p["impl"] for p in plan})
    if arms is None:
        copied = [a for a in impls if unchanged(a)]
        if not copied:
            sys.exit(f"--reuse {old}: no arm's binaries are the ones that run measured; run in full.")
    else:
        unknown = [a for a in arms if a not in impls]
        if unknown:
            sys.exit(f"--reuse-arms: {', '.join(unknown)} not among this run's arms ({', '.join(impls)}).")
        copied = sorted(set(arms))
    if len(copied) == len(impls):
        sys.exit(f"--reuse {old}: every arm would be copied ({', '.join(copied)}), so nothing would be measured.")
    changed = [a for a in copied if not unchanged(a)]

    wanted = {key(p): p for p in plan if p["impl"] in copied}
    rows = []
    with open(os.path.join(old, "runs.jsonl")) as f:
        for line in f:
            row = json.loads(line) if line.strip() else None
            if row and row.get("status") == "ok" and not row.get("control") and key(row) in wanted:
                rows.append(dict(row, reused_from=os.path.basename(os.path.normpath(old))))
    if len(rows) < len(wanted):
        sys.exit(f"--reuse {old}: {len(wanted) - len(rows)} of the runs to copy are not in it; run in full.")
    # A control repeats only a run that measured something, not an arm that a backend or a cell skips.
    picked = sorted((key(r) for r in rows if not r.get("skipped")), key=lambda k: tuple(map(str, k)))
    random.Random(seed + 2).shuffle(picked)
    count = min(len(picked), max(MIN_CONTROLS, round(CONTROL_FRACTION * len(picked))))
    chosen = picked[:count]
    # An arm copied although its binary changed gets at least MIN_CONTROLS of its own, drawn from the same order.
    for arm in changed:
        own = [k for k in chosen if wanted[k]["impl"] == arm]
        more = [k for k in picked[count:] if wanted[k]["impl"] == arm]
        chosen += more[: max(0, MIN_CONTROLS - len(own))]
    controls = [dict(wanted[k], control=True) for k in chosen]
    record = dict(
        source=os.path.basename(os.path.normpath(old)),
        commit=old_meta.get("commit"),
        arms=copied,
        changed_binary=changed,
        reused=len(rows),
        controls=len(controls),
        control_fraction=CONTROL_FRACTION,
        min_controls=MIN_CONTROLS,
        drift_limit=DRIFT,
    )
    return rows, controls, record


def total_ms(run):
    return sum(v["ms"] for v in run["phases"].values())


def drift_line(label, logs, moved="the machine has moved since"):
    """Returns the line on the controls' time against their copies', from the logs of their ratios. `moved` says what
    a ratio past DRIFT means."""
    if len(logs) < 2:
        return f"{label}: {len(logs)} control runs, too few to judge."
    mean = statistics.fmean(logs)
    half = 1.96 * statistics.stdev(logs) / math.sqrt(len(logs))
    lo, mid, hi = math.exp(mean - half), math.exp(mean), math.exp(mean + half)
    verdict = (
        f"within {DRIFT:.0%}"
        if 1 - DRIFT <= lo and hi <= 1 + DRIFT
        else f"WARNING: past {DRIFT:.0%} - {moved}; run in full"
    )
    return (
        f"{label}: {len(logs)} control runs, run again, against the copies: {mid:.3f} [{lo:.3f}, {hi:.3f}] - {verdict}."
    )


def split(runs, key, meta=None):
    """Returns the runs that every ratio is taken over, and the lines about the machine's drift.

    The controls are set apart from the other runs. The first drift line gives the controls' time against the copies
    that they repeat, as a geometric mean with its 95% interval. Each arm that `meta`'s record copied although its
    binary changed gets a line of its own. If no run was copied, the lines are None.
    """
    kept = [r for r in runs if not r.get("control")]
    controls = [r for r in runs if r.get("control") and r.get("status") == "ok" and not r.get("skipped")]
    if not any(r.get("reused_from") for r in kept):
        return kept, None
    copied = {key(r): r for r in kept if r.get("reused_from") and r.get("status") == "ok" and not r.get("skipped")}
    pairs = [(c["impl"], math.log(total_ms(c) / total_ms(copied[key(c)]))) for c in controls if key(c) in copied]
    source = next(r["reused_from"] for r in kept if r.get("reused_from"))
    arms = sorted({r["impl"] for r in kept if r.get("reused_from")})
    lines = [drift_line(f"The runs of {', '.join(arms)} are copied from {source}", [x for _, x in pairs])]
    for arm in ((meta or {}).get("reuse") or {}).get("changed_binary", []):
        logs = [x for a, x in pairs if a == arm]
        lines.append(drift_line(f"{arm}, copied although its binary changed", logs, f"{arm} or the machine has moved"))
    return kept, "\n\n".join(lines)
