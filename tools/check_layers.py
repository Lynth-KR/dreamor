#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that the headers keep their layers (AGENTS.md, "Layout").

    python3 tools/check_layers.py

Reads the #include lines alone, compiles nothing. Three rules:
- a header includes only headers of its own tier or a lower one (TIERS below), and a backend no other backend;
- only a backend includes a solver's header;
- the code users read and copy - examples/, the README, docs/, the skill - never includes dreamor/internal/.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include" / "dreamor"

# Lowest first. A header may include its own tier: the files of a layer use each other, and value_table.hpp and
# the backend layer include each other's parts (a VarTable's solution() makes a ValueTable, which keeps a row index).
TIERS = [
    ("logger.hpp", 0),
    ("internal/base/", 0),
    ("format_buffer.hpp", 1),
    ("internal/index_set/", 2),
    ("entity_vector.hpp", 2),
    ("index_set.hpp", 3),
    ("internal/backend/", 4),
    ("value_table.hpp", 4),
    ("concert.hpp", 5),
    ("mathopt.hpp", 5),
    ("highs.hpp", 5),
]
BACKENDS = {"concert.hpp", "mathopt.hpp", "highs.hpp"}
SOLVER_HEADER = re.compile(r'^\s*#\s*include\s*[<"](ilcplex/|ilconcert/|ortools/|Highs\.h|lp_data/|util/Highs)')
DREAMOR_HEADER = re.compile(r'^\s*#\s*include\s*[<"]dreamor/([^>"]+)[>"]')
USER_FACING = ["examples", "README.md", "docs", "skills"]


def tier_of(header: str) -> int | None:
    for prefix, tier in TIERS:
        if header == prefix or (prefix.endswith("/") and header.startswith(prefix)):
            return tier
    return None


def check_library(problems: list[str]) -> None:
    for path in sorted(INCLUDE.rglob("*.hpp")):
        header = path.relative_to(INCLUDE).as_posix()
        own = tier_of(header)
        if own is None:
            problems.append(f"include/dreamor/{header}: in no tier - add it to TIERS in tools/check_layers.py")
            continue
        for number, line in enumerate(path.read_text().splitlines(), start=1):
            where = f"include/dreamor/{header}:{number}"
            if SOLVER_HEADER.search(line) and header not in BACKENDS:
                problems.append(f"{where}: a solver's header outside a backend")
            match = DREAMOR_HEADER.search(line)
            if not match:
                continue
            target = match.group(1)
            theirs = tier_of(target)
            if theirs is None:
                problems.append(f"{where}: includes dreamor/{target}, which is in no tier")
            elif theirs > own:
                problems.append(f"{where}: includes dreamor/{target}, a layer above ({theirs} > {own})")
            elif header in BACKENDS and target in BACKENDS:
                problems.append(f"{where}: a backend includes another backend, dreamor/{target}")


def check_user_facing(problems: list[str]) -> None:
    for name in USER_FACING:
        base = ROOT / name
        paths = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file())
        for path in paths:
            if path.suffix not in {".hpp", ".cpp", ".cc", ".h", ".md"}:
                continue
            for number, line in enumerate(path.read_text().splitlines(), start=1):
                match = DREAMOR_HEADER.search(line)
                if match and match.group(1).startswith("internal/"):
                    where = f"{path.relative_to(ROOT).as_posix()}:{number}"
                    problems.append(f"{where}: user-facing code includes dreamor/{match.group(1)}")


def main() -> int:
    problems: list[str] = []
    check_library(problems)
    check_user_facing(problems)
    for problem in problems:
        print(problem)
    if problems:
        print(f"check_layers: {len(problems)} problem(s)")
        return 1
    print("check_layers: every header keeps its layer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
