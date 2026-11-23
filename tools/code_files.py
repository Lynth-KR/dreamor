#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# REUSE-IgnoreStart
"""What every code file carries at its top, and what it is made of.

- The SPDX license line comes first, after a #! line if there is one.
- In a file under MPL-2.0, MPL's Exhibit A notice comes right under the SPDX line. A bare comment line separates the
  notice from a comment that follows. Every such file carries the notice, as Eigen's files do, so that the notice
  travels with a file that is copied on its own.
- Every line is ASCII. cl.exe reads a file without a byte-order mark in the system's code page, so one UTF-8
  character in a header is a warning in every Windows build that includes it (docs/dev/platforms.md).

"Every" is what git tracks plus what it would add. A new file counts before it is committed, and an ignored file does
not. Google's samples in examples/mathopt_samples/ keep their own Apache-2.0 line and header. tools/format.sh runs
this script.

    python3 tools/code_files.py           add the license lines where they are missing or misplaced
    python3 tools/code_files.py --check   change nothing; name each file that lacks them

In both modes, the script names every line that is not ASCII and fails, because only a person can fix such a line.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MPL = "SPDX-License-Identifier: MPL-2.0"
MIT0 = "SPDX-License-Identifier: MIT-0"
NOTICE = (
    "This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was",
    "not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.",
)

CPP = ["*.hpp", "*.cpp", "*.cc", "*.h"]
HASH = ["*.py", "*.sh", "*CMakeLists.txt", "*.cmake", "*.cmake.in", "*.Containerfile", "tools/clang-format"]
SAMPLES = [":!examples/mathopt_samples/*.cc"]
# The code that the documents show and users copy is under MIT-0 (README.md, "License"): the examples, except Google's
# samples, and the guide's code. Everything else is under MPL-2.0.
MIT0_CPP = ["examples/*.hpp", "examples/*.cpp", "tests/docs/guide/*.cpp", "tests/docs/guide/*.hpp"]
MIT0_CPP += ["tests/docs/highs/*.cpp", "tests/docs/highs/*.hpp"]
NOT_MIT0 = [":!examples/*.hpp", ":!examples/*.cpp", ":!tests/docs/guide/*", ":!tests/docs/highs/*"]


def files(*pathspecs: str) -> list[Path]:
    """Returns the tracked and the not-yet-added files that match `pathspecs`, except the files that git ignores."""
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *pathspecs],
        cwd=ROOT,
        capture_output=True,
        check=True,
    ).stdout.split(b"\0")
    return [ROOT / name.decode() for name in listed if name and (ROOT / name.decode()).is_file()]


def with_license(lines: list[bytes], line: bytes) -> list[bytes]:
    """Returns the file's lines with `line` in one of the first two lines. If `line` is already there, the lines are
    returned as they are. Otherwise `line` is inserted after a #! line, or at the top if there is none."""
    if line in lines[:2]:
        return lines
    at = 1 if lines and lines[0].startswith(b"#!") else 0
    return lines[:at] + [line] + lines[at:]


def with_notice(lines: list[bytes], prefix: bytes) -> list[bytes]:
    """Returns the file's lines with MPL's notice right under the MPL-2.0 SPDX line. The notice's lines and the bare
    comment lines that stood there are dropped first. If a comment follows the notice, a bare `prefix` line goes
    between them. If neither of the first two lines is the MPL-2.0 SPDX line, the lines are returned as they are."""
    spdx = prefix + b" " + MPL.encode()
    at = next((i for i, line in enumerate(lines[:2]) if line == spdx), None)
    if at is None:
        return lines
    notice = [prefix + b" " + text.encode() for text in NOTICE]
    rest = at + 1
    while rest < len(lines) and lines[rest] in (*notice, prefix):
        rest += 1
    gap = [prefix] if rest < len(lines) and lines[rest].startswith(prefix + b" ") else []
    return lines[: at + 1] + notice + gap + lines[rest:]


def fix(paths: list[Path], change, message: str, check: bool) -> list[str]:
    """Applies `change` to each file's lines, and writes each file that it changes. If `check` is true, the function
    writes nothing and returns a line naming each file that it would change."""
    found = []
    for path in paths:
        lines = path.read_bytes().split(b"\n")
        if lines[-1] == b"":
            lines.pop()  # The final newline leaves an empty last item.
        changed = change(lines)
        if changed == lines:
            continue
        if check:
            found.append(f"{path.relative_to(ROOT)}: {message}")
        else:
            path.write_bytes(b"".join(line + b"\n" for line in changed))  # Writing in place keeps the file's mode.
    return found


def not_ascii(paths: list[Path]) -> list[str]:
    """Returns a line naming each line of `paths` that is not ASCII."""
    found = []
    for path in paths:
        for number, line in enumerate(path.read_bytes().split(b"\n"), 1):
            if not line.isascii():
                text = line.decode(errors="replace")
                found.append(f"{path.relative_to(ROOT)}:{number}:{text}  <- not ASCII")
    return found


def main(argv: list[str]) -> int:
    check = "--check" in argv
    found = []
    for prefix, line, specs in (
        ("//", MPL, CPP + SAMPLES + NOT_MIT0),
        ("//", MIT0, MIT0_CPP),
        ("#", MPL, HASH),
    ):
        entry = f"{prefix} {line}"
        found += fix(files(*specs), lambda lines, e=entry.encode(): with_license(lines, e), f'missing "{entry}"', check)
    for prefix, specs in (("//", CPP + SAMPLES + NOT_MIT0), ("#", HASH)):
        found += fix(
            files(*specs),
            lambda lines, p=prefix.encode(): with_notice(lines, p),
            "MPL's Exhibit A notice is not right under the SPDX line",
            check,
        )
    found += not_ascii(files(*CPP, *HASH))
    for line in found:
        print(line, file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
# REUSE-IgnoreEnd
