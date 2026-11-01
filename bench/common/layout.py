#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A repetition's code layout: the seed the linker shuffles the code's sections with, and the bytes of padding laid
among them.

ASLR moves code by whole pages, so where a function lands within its page and next to which others is fixed by the
build; a repetition that only moves the stack and the heap still runs the same code layout every time. build.sh
(LAYOUTS=n) builds one copy of each binary per repetition, linked by lld with its sections shuffled and a padding
of its own, and run.py runs repetition r on copy r: the expectation a run estimates covers code placement as well
(Mytkowicz et al., ASPLOS 2009; Curtsinger & Berger, ASPLOS 2013). Both follow from the run's seed, so the same
seed, compiler and source give the same binaries again.

    python3 layout.py SEED REP    prints "<shuffle seed> <padding bytes>" (build.sh)
"""

import hashlib
import json
import os
import random
import sys


def code_layout_of(seed, rep):
    """(the linker's shuffle seed, bytes of padding) of a repetition. The shuffle seed is never 0 (lld: a random
    seed) nor 2**32 - 1 (-1: reverse order)."""
    g = random.Random(f"code {seed} {rep}")
    return g.randrange(1, 2**32 - 1), g.randrange(4096)


def laid_out(path, rep):
    """Repetition rep's copy of the binary at path: <its folder>/layout/<rep>/<its name>."""
    return os.path.join(os.path.dirname(path), "layout", str(rep), os.path.basename(path))


def check_layouts(paths, reps, seed):
    """Each repetition's copies of paths, as build.sh recorded them (layout.json) with each copy's sha256 - or exits
    saying how to build them."""
    layouts = {}
    for rep in range(reps):
        for path in paths:
            copy = laid_out(path, rep)
            record = os.path.join(os.path.dirname(copy), "layout.json")
            if not os.path.isfile(copy) or not os.path.isfile(record):
                sys.exit(
                    f"no {os.path.relpath(copy)}: LAYOUTS={reps} LAYOUT_SEED={seed} ./build.sh builds a copy per "
                    "repetition (or run with --layout off)."
                )
            with open(record) as f:
                layout = json.load(f)
            if layout["seed"] != seed:
                sys.exit(f"{os.path.relpath(copy)} was built with LAYOUT_SEED={layout['seed']}, not --seed {seed}.")
            with open(copy, "rb") as f:
                digest = hashlib.sha256(f.read()).hexdigest()
            layouts.setdefault(str(rep), dict(layout, sha256={}))["sha256"][os.path.basename(copy)] = digest
    return layouts


if __name__ == "__main__":
    print(*code_layout_of(int(sys.argv[1]), int(sys.argv[2])))
