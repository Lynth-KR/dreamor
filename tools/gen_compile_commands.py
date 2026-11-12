#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Writes compile_commands.json at the repository root, for clangd.

    python3 tools/gen_compile_commands.py [CPLEX_Studio dir] [OR-Tools dir] [HiGHS dir]

Default CPLEX dir: $CPLEX_STUDIO_DIR, else /opt/ibm/ILOG/CPLEX_Studio2212.
Default OR-Tools dir (an unpacked C++ prebuilt): $ORTOOLS_DIR, else /opt/ortools.
Default HiGHS dir (an install with include/highs): $HIGHS_DIR, else the OR-Tools dir when its prebuilt ships
HiGHS (v9.15 does), else /opt/highs.

Which Concert each file sees:
  - library headers, examples, tests/cplex: the real CPLEX headers (the stub when CPLEX is
    not installed), so the editor shows what the real build sees;
  - the other tests and tests/stub: the Concert stub, which those tests rely on;
  - the MathOpt backend, its example, the ported OR-Tools samples (examples/mathopt_samples)
    and tests/mathopt: OR-Tools (plus the stub, which tests/mathopt/test_cross_backend.cpp
    uses for the Concert side);
  - the HiGHS backend and its parts (internal/highs/), its example, tests/highs and the guide's code that solves
    (tests/docs/highs/): HiGHS's include/highs (plus the stub, which
    tests/highs/test_cross_backend.cpp uses for the Concert side);
  - bench/catalog: the benchmarks' lean stub (bench/common/stub), as bench/catalog/build.sh compiles it; the
    hand-written arms (hand.cpp) without dreamor's include path, so the editor flags a dreamor include there as
    the build does.
The compile_fail and contract_fail topic files are given with no case defined, so clangd reads each case as
an inactive block; to edit one with clangd's help, define its DREAMOR_CASE_<name> at the top for a while.
Warnings are tests/CMakeLists.txt's, without -Werror: strict for dreamor's own code; user for the code users read
(examples/, the guide's under tests/docs/, the README's and the skill's tests), as CMake builds it; -Wall -Wextra for
the ported samples, the compile_fail cases, and bench/.
Re-run after adding files. The output is machine-specific: it is in .gitignore.
"""

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STUDIO = Path(
    sys.argv[1] if len(sys.argv) > 1 else os.environ.get("CPLEX_STUDIO_DIR", "/opt/ibm/ILOG/CPLEX_Studio2212")
)
ORTOOLS = Path(sys.argv[2] if len(sys.argv) > 2 else os.environ.get("ORTOOLS_DIR", "/opt/ortools"))
# HiGHS: as given, else $HIGHS_DIR, else the HiGHS the OR-Tools prebuilt ships, else /opt/highs.
if len(sys.argv) > 3 or "HIGHS_DIR" in os.environ:
    HIGHS = Path(sys.argv[3] if len(sys.argv) > 3 else os.environ["HIGHS_DIR"])
elif (ORTOOLS / "include" / "highs" / "Highs.h").is_file():
    HIGHS = ORTOOLS
else:
    HIGHS = Path("/opt/highs")

BASE = [
    "c++",
    "-std=c++20",
    f"-I{ROOT / 'include'}",
    f"-I{ROOT / 'examples'}",
    f"-I{ROOT / 'tests'}",
]
# Keep in step with strict in tests/CMakeLists.txt (clangd is Clang, so -Wmissing-prototypes too).
STRICT = [
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wshadow",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wold-style-cast",
    "-Wundef",
    "-Wnull-dereference",
    "-Wdouble-promotion",
    "-Wformat-security",
    "-Wmissing-declarations",
    "-Wfloat-equal",
    "-Wcast-align",
    "-Wnon-virtual-dtor",
    "-Wmissing-prototypes",
]
LENIENT = ["-Wall", "-Wextra"]
# user in tests/CMakeLists.txt, but -Werror: the code users read, built as a user builds it.
USER = ["-Wall", "-Wextra"]
USER_CODE = (
    "examples/",
    "tests/docs/guide/",
    "tests/docs/highs/",
)
STUB = [f"-I{ROOT / 'tests' / 'stub'}"]
BENCH = [f"-I{ROOT / 'bench' / 'common' / 'stub'}"]
if (STUDIO / "concert" / "include").is_dir() and (STUDIO / "cplex" / "include").is_dir():
    REAL = ["-DIL_STD", "-isystem", str(STUDIO / "concert" / "include"), "-isystem", str(STUDIO / "cplex" / "include")]
else:
    print(f"CPLEX Studio not found at {STUDIO}: every file uses the Concert stub", file=sys.stderr)
    REAL = STUB
if (ORTOOLS / "include" / "ortools" / "math_opt" / "cpp" / "math_opt.h").is_file():
    MATHOPT = STUB + ["-DOR_PROTO_DLL=", "-isystem", str(ORTOOLS / "include")]
else:
    print(f"OR-Tools not found at {ORTOOLS}: MathOpt files get no OR-Tools include path", file=sys.stderr)
    MATHOPT = STUB
if (HIGHS / "include" / "highs" / "Highs.h").is_file():
    HIGHS_FLAGS = STUB + ["-isystem", str(HIGHS / "include" / "highs")]
else:
    print(f"HiGHS not found at {HIGHS}: HiGHS files get no HiGHS include path", file=sys.stderr)
    HIGHS_FLAGS = STUB


def flags_for(path: Path) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith(("tests/mathopt/", "examples/mathopt_samples/")) or "mathopt" in path.name:
        return MATHOPT
    if rel.startswith("tests/highs/compile_fail/"):
        return HIGHS_FLAGS + [f"-I{ROOT / 'tests' / 'highs' / 'compile_fail'}"]
    if rel.startswith(("tests/highs/", "tests/docs/highs/", "include/dreamor/internal/highs/")) or "highs" in path.name:
        return HIGHS_FLAGS
    if rel.startswith("bench/"):
        return BENCH
    if rel.startswith("tests/compile_fail/"):
        return STUB + [f"-I{ROOT / 'tests' / 'compile_fail'}"]
    if rel.startswith("tests/cplex/") or rel.startswith("examples/") or rel.startswith("include/"):
        return REAL
    return STUB  # everything else under tests/: the Concert stub


def base_for(path: Path) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith("bench/") and path.stem.endswith("hand"):  # bench/catalog/hand.cpp, bench/scenario/*_hand.cpp
        return [a for a in BASE if a != f"-I{ROOT / 'include'}"]
    return BASE


def warnings_for(path: Path) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith(
        (
            "examples/mathopt_samples/",
            "tests/compile_fail/",
            "tests/mathopt/compile_fail/",
            "tests/highs/compile_fail/",
            "bench/",
        )
    ):
        return LENIENT
    if rel.startswith(USER_CODE):
        return USER
    return STRICT


entries = []
for pattern in (
    "include/**/*.hpp",
    "examples/*.hpp",
    "examples/*.cpp",
    "examples/mathopt_samples/*.cc",
    "tests/**/*.hpp",
    "tests/**/*.h",
    "tests/**/*.cpp",
    "tests/**/*.cc",
    "bench/catalog/*.cpp",
    "bench/common/stub/**/*.h",
):
    for path in sorted(ROOT.glob(pattern)):
        if "build" in path.relative_to(ROOT).parts:
            continue
        is_header = path.suffix in (".hpp", ".h")
        args = (
            base_for(path)
            + warnings_for(path)
            + flags_for(path)
            + (["-x", "c++-header"] if is_header else ["-x", "c++"])
            + ["-c", str(path)]
        )
        entries.append({"directory": str(ROOT), "file": str(path), "arguments": args})

out = ROOT / "compile_commands.json"
out.write_text(json.dumps(entries, indent=2) + "\n")
print(f"{out}: {len(entries)} files")
