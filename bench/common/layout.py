#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A repetition's code layout: the seed with which the linker shuffles the code's sections, and the bytes of padding
laid among the sections.

ASLR moves code by whole pages. So the build fixes where a function lands within its page, and which functions lie
next to it. A repetition that only moves the stack and the heap therefore runs the same code layout every time. If
LAYOUTS=n is set, build.sh builds one copy of each binary per repetition, and lld links each copy with its sections
shuffled and a padding of its own. run.py runs repetition r on copy r. The expectation that a run estimates then
covers code placement as well (Mytkowicz et al., ASPLOS 2009; Curtsinger & Berger, ASPLOS 2013). The shuffle seed and
the padding follow from the run's seed, so the same seed, compiler and source give the same binaries again.

build.sh runs the module as a script, which prints "<shuffle seed> <padding bytes>":

    python3 layout.py SEED REP
"""

import hashlib
import json
import os
import random
import sys


def code_layout_of(seed, rep):
    """Returns the linker's shuffle seed and the bytes of padding of a repetition.

    The shuffle seed is never 0, which makes lld choose a random seed. It is never 2**32 - 1 either, which lld reads
    as -1, the reverse order.
    """
    g = random.Random(f"code {seed} {rep}")
    return g.randrange(1, 2**32 - 1), g.randrange(4096)


def laid_out(path, rep):
    """Returns the path of repetition `rep`'s copy of the binary at `path`: <its folder>/layout/<rep>/<its name>."""
    return os.path.join(os.path.dirname(path), "layout", str(rep), os.path.basename(path))


def check_layouts(paths, reps, seed):
    """Returns each repetition's record of its copies of `paths`, as build.sh wrote it in layout.json, with each
    copy's sha256 added.

    If a copy or its record is missing, the function exits and says how to build the copies. If a copy was built with
    another seed than `seed`, the function exits and says so.
    """
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
