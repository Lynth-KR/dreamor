#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# /// script
# requires-python = ">=3.12"
# dependencies = ["libclang==18.1.1"]
# ///
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The library's functions that no test instantiates, which line coverage cannot count, from lcov.py's files.

    uv run tools/coverage/uninstantiated.py build/coverage      # after lcov.py; ORTOOLS_DIR or HIGHS_DIR for those
                                                                # backends' headers

A template that no test instantiates generates no code, so it has no coverage record: it is in neither the numerator
nor the denominator of line coverage, and the figure says nothing of it. libclang lists every function body in
include/dreamor, from programs that include each public header, and a body with no line in any lcov file is one that
no test instantiated. Each is sorted by what it is:

- consteval: runs at compile time alone, and no run can reach it;
- rejection: an overload that exists to stop a misuse with a message, which the compile_fail cases check;
- other: a function a program could run, which no test instantiated - a gap.

libclang is pinned to the LLVM of the coverage preset's Clang, and uv runs it without installing it. --clang names
the compiler whose builtin headers libclang reads (default clang++-18). OR-Tools' generated protobuf headers give
libclang errors, which it reads past: mathopt.hpp's own functions are still found.
"""

import argparse
import collections
import os
import re
import subprocess
import sys
from pathlib import Path

import clang.cindex as ci

ROOT = Path(__file__).resolve().parent.parent.parent
LIBRARY = ROOT / "include" / "dreamor"
HEADERS = [
    "index_set.hpp",
    "entity_vector.hpp",
    "param_table.hpp",
    "solution_table.hpp",
    "format_buffer.hpp",
    "logger.hpp",
    "concert.hpp",
    "highs.hpp",
    "mathopt.hpp",
]
FUNCTIONS = {
    ci.CursorKind.FUNCTION_DECL,
    ci.CursorKind.CXX_METHOD,
    ci.CursorKind.CONSTRUCTOR,
    ci.CursorKind.DESTRUCTOR,
    ci.CursorKind.CONVERSION_FUNCTION,
    ci.CursorKind.FUNCTION_TEMPLATE,
}


def compile_args(clang: str) -> list[str]:
    """The flags of the programs that libclang parses: C++20, the library, the Concert stub, and the solvers' headers
    that the environment names."""
    resource = subprocess.run([clang, "-print-resource-dir"], capture_output=True, text=True, check=True).stdout.strip()
    args = [
        "-x",
        "c++",
        "-std=c++20",
        "-resource-dir",
        resource,
        f"-I{ROOT / 'include'}",
        f"-I{ROOT / 'tests' / 'stub'}",
    ]
    for variable, folder in (("ORTOOLS_DIR", "include"), ("HIGHS_DIR", "include")):
        if os.environ.get(variable):
            args += [f"-I{Path(os.environ[variable]) / folder}", f"-I{Path(os.environ[variable]) / folder / 'highs'}"]
    return args


def bodies(args: list[str]) -> dict[tuple[str, int], int]:
    """Each function body in the library: (file, first line) to its last line."""
    found: dict[tuple[str, int], int] = {}
    for header in HEADERS:
        source = f'#include "dreamor/{header}"\n'
        unit = ci.Index.create().parse(
            "probe.cpp", args=args, unsaved_files=[("probe.cpp", source)], options=ci.TranslationUnit.PARSE_INCOMPLETE
        )
        errors = [d for d in unit.diagnostics if d.severity >= ci.Diagnostic.Error]
        if errors:
            print(f"uninstantiated.py: {header} parsed with {len(errors)} errors, first: {errors[0]}", file=sys.stderr)
        stack = [unit.cursor]
        while stack:
            for child in stack.pop().get_children():
                place = child.location.file
                if place is None:
                    continue
                if not place.name.startswith(str(LIBRARY)):
                    if child.kind == ci.CursorKind.NAMESPACE:
                        stack.append(child)
                    continue
                if child.kind in FUNCTIONS:
                    body = next((g for g in child.get_children() if g.kind == ci.CursorKind.COMPOUND_STMT), None)
                    if body is not None:
                        found[(place.name, body.extent.start.line)] = body.extent.end.line
                stack.append(child)
    return found


def kind_of(lines: list[str], first: int, last: int) -> str:
    """What a body with no record is: its declaration read back to the statement before it."""
    body = "\n".join(lines[first - 1 : last])
    head = [lines[first - 1]]
    at = first - 1
    while at >= 1 and len(head) < 14:
        text = lines[at - 1].strip()
        if not text or text.startswith("//") or text.endswith((";", "{", "}")):
            break
        head.insert(0, lines[at - 1])
        at -= 1
    # The repository marks such an overload "exists only to reject" in its /// block, or says that it catches a
    # misuse "to report it"; a body that is a static_assert of always_false is one too.
    above = "\n".join(lines[max(0, first - 13) : first])
    rejects = re.search(r"exists only to reject|to report it", above)
    if rejects or (re.search(r"static_assert\(\s*(internal::)?always_false", body) and body.count(";") <= 4):
        return "rejection"
    if re.search(r"\bconsteval\b", "\n".join(head)):
        return "consteval"
    return "other"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build", type=Path, help="the coverage preset's build folder, after lcov.py")
    parser.add_argument("--clang", default="clang++-18", help="the compiler whose builtin headers libclang reads")
    args = parser.parse_args()
    lcov = args.build.resolve() / "lcov"
    recorded: dict[str, set[int]] = collections.defaultdict(set)
    for info in sorted(lcov.glob("*.info")):
        source = None
        for row in info.read_text().splitlines():
            if row.startswith("SF:"):
                source = row[3:]
            elif row.startswith("DA:") and source:
                recorded[source].add(int(row[3:].split(",")[0]))
    if not recorded:
        print(f"uninstantiated.py: no lcov files in {lcov} - run lcov.py first", file=sys.stderr)
        return 1
    found = bodies(compile_args(args.clang))
    groups: dict[str, list[tuple[str, int, int, int]]] = collections.defaultdict(list)
    for (source, first), last in sorted(found.items()):
        if set(range(first, last + 1)) & recorded[source]:
            continue
        lines = Path(source).read_text().splitlines()
        code = sum(
            1 for n in range(first, last + 1) if lines[n - 1].strip() and not lines[n - 1].strip().startswith("//")
        )
        groups[kind_of(lines, first, last)].append((str(Path(source).relative_to(LIBRARY)), first, last, code))
    print(f"uninstantiated.py: {len(found)} function bodies, {sum(len(g) for g in groups.values())} with no record")
    for kind in ("consteval", "rejection", "other"):
        rows = groups.get(kind, [])
        print(f"{kind}: {len(rows)} functions, {sum(r[3] for r in rows)} lines of code")
    for source, first, last, code in groups.get("other", []):
        print(f"  {source}:{first}-{last} ({code} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
