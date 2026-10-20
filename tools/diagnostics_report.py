#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
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


def main() -> None:
    compilers = sys.argv[1:] or ["g++"]
    for cxx in compilers:
        print(f"## {cxx}\n")
        print("| case | rejected | lines | errors | first error |")
        print("|---|---|---|---|---|")
        for case in sorted(CASES.glob("*.cpp")):
            run = subprocess.run([cxx, *FLAGS, str(case)], capture_output=True, text=True)
            out = run.stderr
            first = first_error(out).replace("|", "\\|")
            if len(first) > 220:
                first = first[:220] + " ..."
            print(
                f"| {case.stem} | {'yes' if run.returncode else '**NO**'} | {len(out.splitlines())} "
                f"| {out.count('error:')} | {first} |"
            )
        print()


if __name__ == "__main__":
    main()
