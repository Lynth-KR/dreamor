#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds every layer-2 scenario, pmedian and lotsizing, into build/, for each backend that is installed:

    <scenario>_stub     against the lean Concert stub in ../common/stub, so that only the containers are measured.
    <scenario>_cplex    against CPLEX Concert at $CPLEX_STUDIO_DIR. The default is /opt/ibm/ILOG/CPLEX_Studio2212.
    <scenario>_mathopt  against an OR-Tools C++ prebuilt at $ORTOOLS_DIR, which solves with HiGHS. The default is
                        /opt/ortools.
    <scenario>_highs    against HiGHS's own C++ API at $HIGHS_DIR. The default is the HiGHS in the OR-Tools prebuilt.

Each backend gets two binaries:
- <scenario>_<backend> is the dreamor arm, from <scenario>_dreamor.cpp.
- <scenario>_<backend>_hand holds the hand-written arms, from <scenario>_hand.cpp. It is compiled without dreamor's
  include path, so a dreamor header in it does not compile. A change to the library therefore leaves this binary
  byte for byte as it was.
A backend that is not installed is skipped.

    ./build.py              CXX picks the compiler (default g++), JOBS how many compiles run at once
    LAYOUTS=5 ./build.py    also every binary once for each of 5 repetitions, under build/layout/<rep>/

With LAYOUTS=n each repetition's copies have a code layout of their own (../common/build.py), and run.py --layout on
runs repetition r on those copies.
"""

import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common.build import Target, build  # noqa: E402

SCENARIOS = ["pmedian", "lotsizing"]
DREAMOR = ["-I../../include"]  # Only the dreamor arm gets this include path.
STUDIO = Path(os.environ.get("CPLEX_STUDIO_DIR") or "/opt/ibm/ILOG/CPLEX_Studio2212")
ARCH = "x86-64_linux"
ORTOOLS = Path(os.environ.get("ORTOOLS_DIR") or "/opt/ortools")
HIGHS = Path(os.environ.get("HIGHS_DIR") or ORTOOLS)


def backends() -> tuple[dict[str, tuple[list[str], list[str]]], dict[str, str]]:
    """The backends installed here, each with its compile flags and its link flags; and the others, each with why it
    is skipped."""
    found = {"stub": (["-DSCENARIO_STUB", "-I../common/stub"], [])}
    missing = {}
    if (STUDIO / "concert/include").is_dir() and (STUDIO / "cplex/include").is_dir():
        # CPLEX's headers come in through -isystem, so that the compiler does not warn about code that is not ours.
        # IL_STD selects Concert's std:: API.
        found["cplex"] = (
            ["-DIL_STD", "-isystem", f"{STUDIO}/concert/include", "-isystem", f"{STUDIO}/cplex/include"],
            [f"-L{STUDIO}/cplex/lib/{ARCH}/static_pic", f"-L{STUDIO}/concert/lib/{ARCH}/static_pic"]
            + ["-lilocplex", "-lconcert", "-lcplex", "-lm", "-lpthread", "-ldl"],
        )
    else:
        missing["cplex"] = f"no CPLEX Studio at {STUDIO}"
    lib = ORTOOLS / "lib"
    if (ORTOOLS / "include/ortools/math_opt/cpp/math_opt.h").is_file() and lib.is_dir():
        absl = sorted(f"-l{p.name.removeprefix('lib').removesuffix('.so')}" for p in lib.glob("libabsl_*.so"))
        if not absl:
            sys.exit(f"no Abseil libraries (libabsl_*.so) in {lib}")
        found["mathopt"] = (
            ["-DSCENARIO_MATHOPT", "-DOR_PROTO_DLL=", "-isystem", f"{ORTOOLS}/include"],
            [f"-L{lib}", f"-Wl,-rpath,{lib}", "-lortools", "-lprotobuf", *absl],
        )
    else:
        missing["mathopt"] = f"no OR-Tools at {ORTOOLS}"
    if (HIGHS / "include/highs/Highs.h").is_file():
        highs_lib = HIGHS / "lib" if (HIGHS / "lib").is_dir() else HIGHS / "lib64"
        found["highs"] = (
            ["-DSCENARIO_HIGHS", "-isystem", f"{HIGHS}/include/highs"],
            [f"-L{highs_lib}", f"-Wl,-rpath,{highs_lib}", "-lhighs"],
        )
    else:
        missing["highs"] = f"no HiGHS at {HIGHS}"
    return found, missing


def targets(found: dict[str, tuple[list[str], list[str]]]) -> list[Target]:
    """Both arms of every scenario, for each backend in `found`."""
    out = []
    for scenario in SCENARIOS:
        for backend, (flags, libs) in found.items():
            name = f"{scenario}_{backend}"
            out.append(Target(name, f"{scenario}_dreamor.cpp", [*DREAMOR, *flags], libs))
            out.append(Target(f"{name}_hand", f"{scenario}_hand.cpp", flags, libs))
    return out


if __name__ == "__main__":
    os.chdir(HERE)
    found, missing = backends()
    for backend, why in missing.items():
        print(f"skipped {backend}: {why}")
    reps = build(targets(found))
    layouts = f", and {reps} layouts of each" if reps else ""
    print(f"built bench/scenario/build/<scenario>_<backend> and its _hand for {', '.join(found)}{layouts}")
