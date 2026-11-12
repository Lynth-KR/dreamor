#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Hyphens for the dashes of the Markdown documents: an em dash (U+2014) and a minus sign (U+2212) become "-", which
every other page writes. tools/format.sh runs it, in place or with --check, which changes nothing and names each line.

    python3 tools/dashes.py           replace them in every Markdown file git tracks or would add
    python3 tools/dashes.py --check   name each line that has one, and fail
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DASHES = str.maketrans({chr(0x2014): "-", chr(0x2212): "-"})  # by code point: the file stays ASCII


def markdown_files() -> list[Path]:
    listed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.splitlines()
    return [ROOT / name for name in listed if (ROOT / name).is_file()]


def main(argv: list[str]) -> int:
    check = "--check" in argv
    found = False
    for path in markdown_files():
        text = path.read_text(encoding="utf-8")
        fixed = text.translate(DASHES)
        if fixed == text:
            continue
        if check:
            found = True
            for number, line in enumerate(text.splitlines(), 1):
                if line != line.translate(DASHES):
                    print(f"{path.relative_to(ROOT)}:{number}: a dash that is not a hyphen", file=sys.stderr)
        else:
            path.write_text(fixed, encoding="utf-8")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
