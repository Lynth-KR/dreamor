# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""What the parts of the reference's generator share: the paths, the problems found, and a page's names."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
INCLUDE = ROOT / "include" / "dreamor"
IDENT = re.compile(r"[A-Za-z_]\w*")
# A page title's id. Every heading of a page names its id, so that no id is numbered by the site (#select_1): a link to
# a numbered id would land on whatever heading takes that number next, and the strict site build could not tell.
TITLE_ID = "top"


class Problems(list):
    def add(self, path: Path, line: int, text: str) -> None:
        self.append(f"{path.relative_to(ROOT)}:{line}: {text}")


def page_name(title: str) -> str:
    """A section's page: its title in lower case, words joined by hyphens - set-algebra.md, dreamor-highs.md."""
    return "-".join(re.findall(r"[a-z0-9]+", title.lower())) + ".md"
