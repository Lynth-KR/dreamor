# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The README's code, taken out of README.md as written there, so that it is kept in one place.

    python3 tests/docs/readme.py program README.md OUT.cpp   # writes the program; fails if an excerpt is not of it
    python3 tests/docs/readme.py output README.md PROGRAM...  # runs it; fails if the lines the README shows are not
                                                              # in its output, in that order

The program is the one ```cpp block that holds `int main(`. Every other ```cpp block is an excerpt of it: its lines of
code - a comment line may be the excerpt's own - come in the program in the same order, indentation aside. The output
is the ```text block after the program: its lines come in what the program prints, in order, with any other line
(the solver's log) between them.
"""

import re
import subprocess
import sys
from pathlib import Path


def blocks(readme: Path, language: str) -> list[tuple[int, list[str]]]:
    """The README's fenced blocks of a language: each one's first line number and its lines."""
    found, current, start = [], None, 0
    for number, line in enumerate(readme.read_text(encoding="utf-8").splitlines(), 1):
        if current is None and line == f"```{language}":
            current, start = [], number + 1
        elif current is not None and line == "```":
            found.append((start, current))
            current = None
        elif current is not None:
            current.append(line)
    return found


def program(readme: Path) -> tuple[int, list[str]]:
    mains = [block for block in blocks(readme, "cpp") if any(re.match(r"int main\(", line) for line in block[1])]
    if len(mains) != 1:
        sys.exit(f"{readme}: {len(mains)} ```cpp blocks hold int main(, not one")
    return mains[0]


def code(lines: list[str]) -> list[str]:
    """The lines of code, indentation aside: no blank line, no comment line."""
    return [line.strip() for line in lines if line.strip() and not line.strip().startswith("//")]


def write_program(readme: Path, out: Path) -> int:
    start, lines = program(readme)
    whole = code(lines)
    for excerpt_start, excerpt in blocks(readme, "cpp"):
        if excerpt_start == start:
            continue
        at = 0
        for line in code(excerpt):
            try:
                at = whole.index(line, at) + 1
            except ValueError:
                sys.exit(f"{readme}:{excerpt_start}: an excerpt's line is not in the program, in its order: {line}")
    text = (
        f"// Taken out of {readme.name}, lines {start} on, by tests/docs/readme.py: edit the README, not this file.\n"
    )
    out.write_text(text + "\n".join(lines) + "\n", encoding="utf-8")
    return 0


def check_output(readme: Path, command: list[str]) -> int:
    start, _ = program(readme)
    shown = [block for block in blocks(readme, "text") if block[0] > start]
    if not shown:
        sys.exit(f"{readme}: no ```text block after the program, to hold what it prints")
    expected = [line for line in shown[0][1] if line.strip()]
    printed = subprocess.run(command, capture_output=True, text=True, check=False)
    if printed.returncode != 0:
        sys.stderr.write(printed.stdout + printed.stderr)
        sys.exit(f"the README's program exited with {printed.returncode}")
    lines = printed.stdout.splitlines()
    at = 0
    for line in expected:
        try:
            at = lines.index(line, at) + 1
        except ValueError:
            sys.stderr.write(printed.stdout)
            sys.exit(f"{readme}:{shown[0][0]}: the program does not print, in this order: {line}")
    print(f"readme           ok  ({len(expected)} lines as the README shows them)")
    return 0


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "program":
        return write_program(Path(sys.argv[2]), Path(sys.argv[3]))
    if len(sys.argv) >= 4 and sys.argv[1] == "output":
        return check_output(Path(sys.argv[2]), sys.argv[3:])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
