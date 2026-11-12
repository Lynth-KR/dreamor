#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The skill's copies of the documentation: skills/dreamor/ is copied into a project alone, so it carries the guide and
the API reference with it, as files an agent reads.

    python3 tools/sync_skill_docs.py            # writes skills/dreamor/docs/: a copy of each page, and none other
    python3 tools/sync_skill_docs.py --check    # fails if a copy is not what it would write, or one is missing or left

docs/guide/*.md and docs/reference/*.md are copied to skills/dreamor/docs/guide/ and .../reference/, so the links
between them hold. A guide page's --8<-- "path:name" lines - its code, pulled from a test by Zensical - are written out
as the region's lines, unindented as Zensical does. Each copy's first line names its source and the git blob hash of
every file it is made from (git hash-object): edit the source, never the copy.
"""

import argparse
import hashlib
import re
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = (ROOT / "docs" / "guide", ROOT / "docs" / "reference")
COPIES = ROOT / "skills" / "dreamor" / "docs"
SNIPPET = re.compile(r'^--8<-- "([^":]+):([^"]+)"$')


def blob(data: bytes) -> str:
    """The file's git blob hash, as git hash-object gives it."""
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def region(path: Path, name: str) -> list[str]:
    """The lines between a file's --8<-- [start:name] and [end:name], unindented."""
    lines = path.read_text(encoding="utf-8").split("\n")
    starts = [k for k, line in enumerate(lines) if line.strip().endswith(f"--8<-- [start:{name}]")]
    ends = [k for k, line in enumerate(lines) if line.strip().endswith(f"--8<-- [end:{name}]")]
    if len(starts) != 1 or len(ends) != 1 or ends[0] < starts[0]:
        sys.exit(f"{path.relative_to(ROOT)}: no region {name}, or more than one")
    return textwrap.dedent("\n".join(lines[starts[0] + 1 : ends[0]])).split("\n")


def copy_of(page: Path) -> str:
    """A page's copy: its source and the files it pulls code from named with their hashes, then the page, its
    snippet lines written out."""
    made_from = [page]
    out = []
    for line in page.read_text(encoding="utf-8").split("\n"):
        found = SNIPPET.match(line)
        if found:
            source = ROOT / found.group(1)
            if source not in made_from:
                made_from.append(source)
            out += region(source, found.group(2))
        else:
            out.append(line)
    names = ", ".join(f"{p.relative_to(ROOT).as_posix()} (blob {blob(p.read_bytes())[:12]})" for p in made_from)
    header = f"<!-- Copied from {names} by tools/sync_skill_docs.py - edit the source, not this copy. -->"
    return header + "\n\n" + "\n".join(out)


def copies() -> dict[Path, str]:
    return {
        COPIES / source.name / page.name: copy_of(page) for source in SOURCES for page in sorted(source.glob("*.md"))
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if the copies are not what would be written")
    args = parser.parse_args()
    wanted = copies()
    present = set(COPIES.glob("*/*.md"))
    if args.check:
        stale = sorted(p.relative_to(ROOT).as_posix() for p in present - set(wanted))
        wrong = sorted(
            p.relative_to(ROOT).as_posix()
            for p, text in wanted.items()
            if not p.is_file() or p.read_text(encoding="utf-8") != text
        )
        for path in stale:
            print(f"{path}: a copy of no page", file=sys.stderr)
        for path in wrong:
            print(f"{path}: not what tools/sync_skill_docs.py writes", file=sys.stderr)
        if stale or wrong:
            print("run python3 tools/sync_skill_docs.py", file=sys.stderr)
            return 1
        return 0
    for path in present - set(wanted):
        path.unlink()
    for path, text in wanted.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
