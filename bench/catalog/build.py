#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds build/catalog from dreamor.cpp, against the dreamor headers in ../../include, and build/catalog_hand from
hand.cpp, without them. For k = 8 it builds build/catalog8 and build/catalog_hand8 the same way. Every build uses the
lean Concert stub in ../common/stub.

    ./build.py              CXX picks the compiler (default g++), JOBS how many compiles run at once
    LAYOUTS=5 ./build.py    also every binary once for each of 5 repetitions, under build/layout/<rep>/

With LAYOUTS=n each repetition's copies have a code layout of their own (../common/build.py), and run.py --layout on
runs repetition r on the binaries of r.
"""

import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common.build import Target, build  # noqa: E402

STUB = ["-I../common/stub"]
WIDE = ["-DCATALOG_MAX_COLUMNS=8"]
# The dreamor arm is built with the library's include path. The hand-written arms are built without it, so that a
# dreamor header in them fails the build, and a change to the library leaves their binaries the same, byte for byte.
DREAMOR = ["-I../../include"]


def targets() -> list[Target]:
    """The four binaries. k = 8 has builds of its own, in which the hand-written arms and the samples hold rows 8
    wide, as data.hpp describes."""
    return [
        Target("catalog", "dreamor.cpp", [*STUB, *DREAMOR]),
        Target("catalog8", "dreamor.cpp", [*STUB, *DREAMOR, *WIDE]),
        Target("catalog_hand", "hand.cpp", STUB),
        Target("catalog_hand8", "hand.cpp", [*STUB, *WIDE]),
    ]


if __name__ == "__main__":
    os.chdir(HERE)
    reps = build(targets())
    layouts = f", and {reps} layouts of each" if reps else ""
    print(f"built bench/catalog/build/catalog, catalog8, catalog_hand and catalog_hand8{layouts}")
