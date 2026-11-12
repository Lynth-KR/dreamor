#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that the headers keep their layers (AGENTS.md, "Layout").

    python3 tools/check_layers.py

Reads the #include lines alone, compiles nothing. Five rules:
- a header includes only headers of its own tier or a lower one (TIERS below), and a backend no other backend;
- set-builder notation is the chain, written as the notation reads (docs/dev/design.md, "set_builder is the chain"):
  set_builder.hpp includes the chain, never the engine under it (KEPT_OUT below);
- a standalone header (STANDALONE below) includes no other header of dreamor's, so that it can be taken out whole;
- only a backend includes a solver's header;
- the code users read and copy - examples/, the README, docs/, the skill - never includes dreamor/internal/;
- a header opens its namespaces as docs/dev/code-style.md, "A header's order", says: each at file scope by its full
  name, once, the internal ones (internal, detail) before the public ones - but for the blocks of forward
  declarations alone that open it - and none inside another.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include" / "dreamor"

# Lowest first. A header may include its own tier: the files of a layer use each other, and solution_table.hpp and
# the backend layer include each other's parts (a VarTable's solution() makes a SolutionTable, which keeps a row
# index); param_table.hpp keeps one too.
TIERS = [
    ("logger.hpp", 0),
    ("internal/base/", 0),
    ("format_buffer.hpp", 1),
    ("internal/index_set/", 2),
    ("entity_vector.hpp", 2),
    ("index_set.hpp", 3),
    ("internal/backend/", 4),
    ("solution_table.hpp", 4),
    ("param_table.hpp", 4),
    ("concert.hpp", 5),
    ("mathopt.hpp", 5),
    ("highs.hpp", 5),
    ("internal/highs/", 5),
]
BACKENDS = {"concert.hpp", "mathopt.hpp", "highs.hpp"}
# A backend's own parts, behind its header: they include its solver's headers as the backend does.
BACKEND_PARTS = ("internal/highs/",)
STANDALONE: set[str] = set()
# Headers a header may not include, though their tier allows it: what it is a layer over, it reaches through that.
KEPT_OUT = {
    "internal/index_set/set_builder.hpp": {"internal/index_set/set.hpp", "internal/index_set/plan.hpp"},
}
SOLVER_HEADER = re.compile(r'^\s*#\s*include\s*[<"](ilcplex/|ilconcert/|ortools/|Highs\.h|lp_data/|util/Highs)')
DREAMOR_HEADER = re.compile(r'^\s*#\s*include\s*[<"]dreamor/([^>"]+)[>"]')
USER_FACING = ["examples", "README.md", "docs", "skills"]
NAMESPACE_OPEN = re.compile(r"^(\s*)(?:inline\s+)?namespace\s+([^{]+?)\s*\{\s*(?://.*)?$")
NAMESPACE_CLOSE = re.compile(r"^\s*\}\s*//\s*namespace\b")
PRIVATE_NAMESPACES = {"internal", "detail"}


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
            if SOLVER_HEADER.search(line) and header not in BACKENDS and not header.startswith(BACKEND_PARTS):
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
            elif header in STANDALONE:
                problems.append(f"{where}: includes dreamor/{target}, and a standalone header includes none")
            elif target in KEPT_OUT.get(header, set()):
                problems.append(
                    f"{where}: includes dreamor/{target}, which it reaches only through the layer it is over"
                )
            elif header in BACKENDS and target in BACKENDS:
                problems.append(f"{where}: a backend includes another backend, dreamor/{target}")


def namespace_problems(lines: list[str]) -> list[str]:
    """What breaks the namespace order in one header's lines."""
    found, blocks, depth, current = [], [], 0, None
    for number, line in enumerate(lines, start=1):
        opened = NAMESPACE_OPEN.match(line)
        if opened:
            if depth:
                found.append(f"{number}: a namespace opened inside another - open it at file scope by its full name")
            name = re.sub(r"\binline\s+|\s+", "", opened.group(2))
            depth += 1
            if depth == 1:
                current = {"name": name, "line": number, "body": []}
            continue
        if NAMESPACE_CLOSE.match(line):
            depth -= 1
            if depth == 0 and current:
                blocks.append(current)
                current = None
            continue
        if current is not None and line.strip() and not line.lstrip().startswith("//"):
            current["body"].append(line)
    seen: set[str] = set()
    public_from = None
    leading = True  # the blocks of declarations alone that open a header
    for block in blocks:
        declarations_only = not any("{" in line for line in block["body"])
        if declarations_only and leading:
            continue
        leading = False
        name, number = block["name"], block["line"]
        if name in seen:
            found.append(f"{number}: namespace {name} opened a second time - one block a namespace")
        seen.add(name)
        private = bool(PRIVATE_NAMESPACES & set(name.split("::")))
        if private and public_from is not None:
            found.append(f"{number}: internal namespace {name} after the public {public_from} - the helpers go first")
        if not private and public_from is None:
            public_from = name
    return found


def check_namespaces(problems: list[str]) -> None:
    for path in sorted(INCLUDE.rglob("*.hpp")):
        header = path.relative_to(INCLUDE).as_posix()
        found = namespace_problems(path.read_text().splitlines())
        problems += [f"include/dreamor/{header}:{problem}" for problem in found]


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
    check_namespaces(problems)
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
