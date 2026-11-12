#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The API reference, generated from the headers' /// comments (docs/dev/comments.md says how they are written).

    python3 tools/gen_reference.py                  # the format's checks alone: names every problem, exit 1 if any
    python3 tools/gen_reference.py --out DIR        # and writes the reference: a page a section, and index.md; for
                                                    # docs/reference, its pages in zensical.toml's nav too
    python3 tools/gen_reference.py --check DIR      # and fails if DIR's pages, or that nav, are not what it would write
    python3 tools/gen_reference.py --list           # every declaration a user names, documented or not

The parts are in tools/refgen/: scan.py reads each header's declarations and /// blocks without parsing C++,
entries.py makes them the entries of the sections and checks the format, render.py writes the pages - each
signature through the pinned clang-format at 88 columns - and the site's nav.
"""

import argparse
import sys
from pathlib import Path

from refgen.common import ROOT, Problems
from refgen.entries import Reference
from refgen.render import render, with_nav

SITE_CONFIG = ROOT / "zensical.toml"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", help="write the reference's pages into this folder")
    parser.add_argument("--check", help="fail if this folder's pages are not what would be written")
    parser.add_argument("--list", action="store_true", help="list every declaration a user names")
    args = parser.parse_args()
    problems = Problems()
    reference = Reference(problems)
    if args.list:
        for decl in reference.declared:
            mark = "+" if decl.doc else "-"
            qualified = "::".join(filter(None, [decl.scope, decl.name]))
            print(f"{mark} {decl.path.relative_to(ROOT)}:{decl.line}: {decl.kind} {qualified}")
        return 0
    for problem in problems:
        print(problem, file=sys.stderr)
    if args.out or args.check:
        pages = render(reference)
        if args.out:
            folder = Path(args.out)
            folder.mkdir(parents=True, exist_ok=True)
            for stale in set(p.name for p in folder.glob("*.md")) - set(pages):
                (folder / stale).unlink()
            for name, text in pages.items():
                (folder / name).write_text(text, encoding="utf-8")
            if folder.resolve() == ROOT / "docs" / "reference":
                SITE_CONFIG.write_text(with_nav(SITE_CONFIG.read_text(encoding="utf-8"), pages), encoding="utf-8")
        if args.check:
            folder = Path(args.check)
            written = {p.name: p.read_text(encoding="utf-8") for p in folder.glob("*.md")} if folder.is_dir() else {}
            config = SITE_CONFIG.read_text(encoding="utf-8")
            stale_nav = folder.resolve() == ROOT / "docs" / "reference" and with_nav(config, pages) != config
            if written != pages or stale_nav:
                print(
                    f"{args.check} is not what tools/gen_reference.py writes: run it with --out {args.check}",
                    file=sys.stderr,
                )
                return 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
