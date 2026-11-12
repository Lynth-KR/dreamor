#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""How readable are the compiler's errors for the mistakes in tests/compile_fail/?

    python3 tools/diagnostics_report.py [compiler ...]      (default: g++)

For each case and compiler: whether it was rejected, how many lines and how many `error:`
lines the compiler printed, and the first error - the line a user actually reads. Uses the
Concert stub, so no CPLEX is needed.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tests" / "compile_fail"
FLAGS = ["-std=c++20", "-fsyntax-only", f"-I{ROOT / 'include'}", f"-I{ROOT / 'tests' / 'stub'}", f"-I{CASES}"]


def first_error(text: str) -> str:
    for line in text.splitlines():
        m = re.search(r"error: (.*)", line)
        if m:
            return m.group(1)
    return ""


def cases() -> list[tuple[str, Path]]:
    """Every case of the topic files, by name: `#ifdef DREAMOR_CASE_<name>` to its `#endif`."""
    found = []
    for topic in sorted(CASES.glob("*.cpp")):
        for line in topic.read_text().splitlines():
            m = re.fullmatch(r"#ifdef DREAMOR_CASE_(\w+)", line)
            if m:
                found.append((m.group(1), topic))
    return sorted(found)


def main() -> None:
    compilers = sys.argv[1:] or ["g++"]
    for cxx in compilers:
        print(f"## {cxx}\n")
        print("| case | rejected | lines | errors | first error |")
        print("|---|---|---|---|---|")
        for name, topic in cases():
            run = subprocess.run([cxx, *FLAGS, f"-DDREAMOR_CASE_{name}", str(topic)], capture_output=True, text=True)
            out = run.stderr
            first = first_error(out).replace("|", "\\|")
            if len(first) > 220:
                first = first[:220] + " ..."
            print(
                f"| {name} | {'yes' if run.returncode else '**NO**'} | {len(out.splitlines())} "
                f"| {out.count('error:')} | {first} |"
            )
        print()


if __name__ == "__main__":
    main()
