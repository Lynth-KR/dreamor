#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Writes compile_commands.json at the repository root, for clangd.

    python3 tools/gen_compile_commands.py [CPLEX_Studio dir] [OR-Tools dir]

Default CPLEX dir: $CPLEX_STUDIO_DIR, else /opt/ibm/ILOG/CPLEX_Studio2212.
Default OR-Tools dir (an unpacked C++ prebuilt): $ORTOOLS_DIR, else /opt/ortools.

Which Concert each file sees:
  - library headers, examples, tests/cplex: the real CPLEX headers (the stub when CPLEX is
    not installed), so the editor shows what the real build sees;
  - the other tests and tests/stub: the Concert stub, which those tests rely on;
  - the MathOpt backend, its example, the ported OR-Tools samples (examples/mathopt_samples)
    and tests/mathopt: OR-Tools (plus the stub, which tests/mathopt/test_cross_backend.cpp
    uses for the Concert side);
  - bench/catalog: the benchmarks' lean stub (bench/common/stub), as bench/catalog/build.sh compiles it.
tests/compile_fail/*.cpp fail to compile on purpose; clangd reports those errors too.
Warnings are tests/run_tests.sh's STRICT set (without -Werror) for dreamor's own code, and
-Wall -Wextra for the ported samples, the compile_fail cases, and bench/.
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

BASE = [
    "c++",
    "-std=c++20",
    f"-I{ROOT / 'include'}",
    f"-I{ROOT / 'examples'}",
    f"-I{ROOT / 'tests'}",
]
# Keep in step with STRICT in tests/run_tests.sh (clangd is Clang, so -Wmissing-prototypes too).
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


def flags_for(path: Path) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith(("tests/mathopt/", "examples/mathopt_samples/")) or "mathopt" in path.name:
        return MATHOPT
    if rel.startswith("bench/"):
        return BENCH
    if rel.startswith("tests/compile_fail/"):
        return STUB + [f"-I{ROOT / 'tests' / 'compile_fail'}"]
    if rel.startswith("tests/cplex/") or rel.startswith("examples/") or rel.startswith("include/"):
        return REAL
    return STUB  # tests/*.cpp, tests/check.hpp, tests/stub/**


def warnings_for(path: Path) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith(("examples/mathopt_samples/", "tests/compile_fail/", "tests/mathopt/compile_fail/", "bench/")):
        return LENIENT
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
            BASE
            + warnings_for(path)
            + flags_for(path)
            + (["-x", "c++-header"] if is_header else ["-x", "c++"])
            + ["-c", str(path)]
        )
        entries.append({"directory": str(ROOT), "file": str(path), "arguments": args})

out = ROOT / "compile_commands.json"
out.write_text(json.dumps(entries, indent=2) + "\n")
print(f"{out}: {len(entries)} files")
