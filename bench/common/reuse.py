#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Measuring a new version of dreamor against hand-written arms measured before (run.py --reuse).

The hand-written arms are built without the library (build.sh), so a change to dreamor leaves their
binaries - and each repetition's copy of them - byte for byte as they were. A run of a new version
then needs only the dreamor arm: run.py --reuse <earlier run> copies the earlier run's hand-written
runs into the new one, where its resuming leaves them be, and runs the rest. A repetition's memory
and code layout come from the seed, so a new dreamor run and an earlier hand-written run of the same
repetition stay paired.

The copies are taken only when what made them is what the new run would make: the same machine,
compiler, seed, repetitions and design, and every hand-written binary of every repetition the same
sha256 as the earlier run recorded. And since the machine itself may have moved since (a kernel, a
firmware, its temperature, what else it runs), a tenth of the hand-written runs that measured
something - at least 30, or all of them if fewer - drawn by the seed across the cells and
repetitions, run again as controls: report.py shows their time against the copies' and warns when
the 95% interval of that ratio leaves 1 +- 2% - then the whole run is to be made again. The controls
stay out of every ratio.
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


def copies(old, meta, same, code_layouts, hand, plan, key, seed):
    """Checks that the earlier run at old is one the new run can take its hand-written arms from, and
    returns (the earlier run's hand-written rows, marked as copies, for the plan's runs; the plan's
    runs to make again as controls; what meta records). same: the meta fields both runs must share.
    hand: the hand-written binaries' names. Exits saying why it cannot."""
    with open(os.path.join(old, "meta.json")) as f:
        old_meta = json.load(f)
    for field in same:
        if old_meta.get(field) != meta.get(field):
            sys.exit(f"--reuse {old}: {field} differs ({old_meta.get(field)!r}, now {meta.get(field)!r}); run in full.")
    if not code_layouts or not old_meta.get("code_layouts"):
        sys.exit(f"--reuse {old}: both runs need --layout on, whose copies are checked by their sha256.")
    for rep, layout in code_layouts.items():
        for name in hand:
            was = old_meta["code_layouts"].get(rep, {}).get("sha256", {}).get(name)
            if was != layout["sha256"].get(name):
                sys.exit(f"--reuse {old}: repetition {rep}'s {name} is not the binary that run measured; run in full.")
    wanted = {key(p): p for p in plan if p["impl"] != "dreamor"}
    rows = []
    with open(os.path.join(old, "runs.jsonl")) as f:
        for line in f:
            row = json.loads(line) if line.strip() else None
            if row and row.get("status") == "ok" and not row.get("control") and key(row) in wanted:
                rows.append(dict(row, reused_from=os.path.basename(os.path.normpath(old))))
    if len(rows) < len(wanted):
        sys.exit(f"--reuse {old}: {len(wanted) - len(rows)} of the hand-written runs are not in it; run in full.")
    # A control repeats a run that measured something: not an arm a backend or a cell skips.
    picked = sorted((key(r) for r in rows if not r.get("skipped")), key=lambda k: tuple(map(str, k)))
    random.Random(seed + 2).shuffle(picked)
    count = min(len(picked), max(MIN_CONTROLS, round(CONTROL_FRACTION * len(picked))))
    controls = [dict(wanted[k], control=True) for k in picked[:count]]
    record = dict(
        source=os.path.basename(os.path.normpath(old)),
        commit=old_meta.get("commit"),
        reused=len(rows),
        controls=len(controls),
        control_fraction=CONTROL_FRACTION,
        min_controls=MIN_CONTROLS,
        drift_limit=DRIFT,
    )
    return rows, controls, record


def total_ms(run):
    return sum(v["ms"] for v in run["phases"].values())


def split(runs, key):
    """(the runs every ratio is taken over, the drift line or None): controls set apart, and their
    time against the copies they repeat, a geometric mean with its 95% interval."""
    kept = [r for r in runs if not r.get("control")]
    controls = [r for r in runs if r.get("control") and r.get("status") == "ok" and not r.get("skipped")]
    if not any(r.get("reused_from") for r in kept):
        return kept, None
    copied = {key(r): r for r in kept if r.get("reused_from") and r.get("status") == "ok" and not r.get("skipped")}
    logs = [math.log(total_ms(c) / total_ms(copied[key(c)])) for c in controls if key(c) in copied]
    source = next(r["reused_from"] for r in kept if r.get("reused_from"))
    if len(logs) < 2:
        return kept, f"The hand-written arms are copied from {source}, with {len(logs)} control runs: too few to judge."
    mean = statistics.fmean(logs)
    half = 1.96 * statistics.stdev(logs) / math.sqrt(len(logs))
    lo, mid, hi = math.exp(mean - half), math.exp(mean), math.exp(mean + half)
    verdict = (
        f"within {DRIFT:.0%}"
        if 1 - DRIFT <= lo and hi <= 1 + DRIFT
        else f"WARNING: past {DRIFT:.0%} - the machine has moved since; run in full"
    )
    return kept, (
        f"The hand-written arms are copied from {source}. Their {len(logs)} control runs, run again, against the "
        f"copies: {mid:.3f} [{lo:.3f}, {hi:.3f}] - {verdict}."
    )
