#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that the benchmarks still compile against the headers. Each source of each variant that a build script
builds is compiled for its syntax only.

    python3 tools/check_bench.py        # CXX (default g++-13), ORTOOLS_DIR for the MathOpt variants and HIGHS_DIR
                                        # (default ORTOOLS_DIR) for the HiGHS ones (each skipped without)

The benchmarks run by hand, never in CI; this keeps a change to the headers from breaking them unseen. The catalog's
and the scenarios' variants are their build.py's own targets, with the same flags. CPLEX variants are left out: CI
has no CPLEX, and the stub variants compile the same model code.
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
sys.path.insert(0, str(BENCH))
from catalog import build as catalog  # noqa: E402
from scenario import build as scenario  # noqa: E402

BASE = ["-std=c++20", "-fsyntax-only", "-DNDEBUG", "-Wall", "-Wextra", "-Werror"]
INCLUDE = ["-I", str(ROOT / "include")]


def variants() -> list[tuple[str, Path, list[str]]]:
    """Returns each variant that a build script builds: a label, the folder it compiles in, and the compiler's
    arguments."""
    out = [(f"catalog {t.out}", BENCH / "catalog", [*t.flags, t.source]) for t in catalog.targets()]
    found, missing = scenario.backends()
    found.pop("cplex", None)
    for backend, why in missing.items():
        if backend != "cplex":
            print(f"{why}: the {backend} variants are skipped", file=sys.stderr)
    out += [(f"scenario {t.out}", BENCH / "scenario", [*t.flags, t.source]) for t in scenario.targets(found)]
    # The three programs that bench/plan/build.sh builds.
    plan = BENCH / "plan"
    out.append(("plan model", plan, [*INCLUDE, "model.cpp"]))
    out.append(("plan", plan, [*INCLUDE, "plan.cpp"]))
    out.append(("plan PLAN_COUNT", plan, [*INCLUDE, "-DPLAN_COUNT", "plan.cpp"]))
    return out


def main() -> int:
    cxx = os.environ.get("CXX", "g++-13")
    jobs = int(os.environ.get("JOBS", str(max(1, (os.cpu_count() or 2) // 2))))

    def compile_one(variant: tuple[str, Path, list[str]]) -> tuple[str, subprocess.CompletedProcess]:
        label, folder, arguments = variant
        command = [cxx, *BASE, *arguments]
        return label, subprocess.run(command, cwd=folder, capture_output=True, text=True, check=False)

    failed = 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for label, run in pool.map(compile_one, variants()):
            if run.returncode != 0:
                failed += 1
                print(f"bench: {label} does not compile\n{run.stderr}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
