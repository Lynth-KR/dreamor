#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The benchmarks still compile against the headers: each source in each variant its build.sh builds, syntax only.

    python3 tools/check_bench.py        # CXX (default g++-13), ORTOOLS_DIR for the MathOpt variants and HIGHS_DIR
                                        # (default ORTOOLS_DIR) for the HiGHS ones (each skipped without)

The benchmarks run by hand, never in CI; this keeps a change to the headers from breaking them unseen. CPLEX
variants are left out: CI has no CPLEX, and the stub variants compile the same model code.
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
STUB = ["-I", str(BENCH / "common" / "stub")]
BASE = ["-std=c++20", "-fsyntax-only", "-DNDEBUG", "-Wall", "-Wextra", "-Werror"]
# dreamor's headers, named by each variant that may see them: not the hand-written arms', as their build.sh has it.
INCLUDE = ["-I", str(ROOT / "include")]


def variants() -> list[tuple[str, list[str]]]:
    """Each source and the flags of one variant its build.sh builds: (label, arguments)."""
    out = []
    catalog = BENCH / "catalog"
    for extra in ([], ["-DCATALOG_MAX_COLUMNS=8"]):
        out.append((f"catalog dreamor {' '.join(extra)}", [*INCLUDE, *STUB, *extra, str(catalog / "dreamor.cpp")]))
        out.append((f"catalog hand {' '.join(extra)}", [*STUB, *extra, str(catalog / "hand.cpp")]))
    ortools = Path(os.environ.get("ORTOOLS_DIR", "/opt/ortools"))
    mathopt = (ortools / "include" / "ortools" / "math_opt" / "cpp" / "math_opt.h").is_file()
    mathopt_flags = ["-DOR_PROTO_DLL=", "-isystem", str(ortools / "include")]
    highs = Path(os.environ.get("HIGHS_DIR", str(ortools))) / "include" / "highs"
    highs_flags = ["-DSCENARIO_HIGHS", "-isystem", str(highs)] if (highs / "Highs.h").is_file() else None
    for scenario in ("pmedian", "lotsizing"):
        for arm, include in (("dreamor", INCLUDE), ("hand", [])):
            source = str(BENCH / "scenario" / f"{scenario}_{arm}.cpp")
            out.append((f"{scenario} stub {arm}", [*include, *STUB, "-DSCENARIO_STUB", source]))
            if mathopt:
                out.append((f"{scenario} mathopt {arm}", [*include, "-DSCENARIO_MATHOPT", *mathopt_flags, source]))
            if highs_flags:
                out.append((f"{scenario} highs {arm}", [*include, *highs_flags, source]))
    out.append(("plan model", [*INCLUDE, str(BENCH / "plan" / "model.cpp")]))
    out.append(("plan", [*INCLUDE, str(BENCH / "plan" / "plan.cpp")]))
    out.append(("plan PLAN_COUNT", [*INCLUDE, "-DPLAN_COUNT", str(BENCH / "plan" / "plan.cpp")]))
    if not mathopt:
        print(f"no OR-Tools at {ortools}: the MathOpt variants are skipped", file=sys.stderr)
    return out


def main() -> int:
    cxx = os.environ.get("CXX", "g++-13")
    jobs = int(os.environ.get("JOBS", str(max(1, (os.cpu_count() or 2) // 2))))

    def compile_one(variant: tuple[str, list[str]]) -> tuple[str, subprocess.CompletedProcess]:
        label, arguments = variant
        return label, subprocess.run([cxx, *BASE, *arguments], capture_output=True, text=True, check=False)

    failed = 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for label, run in pool.map(compile_one, variants()):
            if run.returncode != 0:
                failed += 1
                print(f"bench: {label} does not compile\n{run.stderr}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
