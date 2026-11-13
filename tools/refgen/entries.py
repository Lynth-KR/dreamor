# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The entries: each /// block split into its summary, body and fields, gathered into the sections of
reference_sections.txt, with the checks of the format (docs/dev/comments.md), the overloads joined to their entries,
the headings' ids and the contracts linked to their contract_fail cases.
"""

import collections
import dataclasses
import re
from pathlib import Path

from refgen.common import IDENT, INCLUDE, ROOT, TITLE_ID, Problems, page_name
from refgen.messages import MessageSection, read_messages
from refgen.scan import Decl, Doc, Scanner, strip_template_header

SECTIONS = ROOT / "tools" / "reference_sections.txt"
CONTRACT_CASES = (ROOT / "tests" / "contract_fail", ROOT / "tests" / "highs" / "contract_fail")
FIELDS = ("Takes", "Returns", "Throws", "Contract", "Cost")
MARKERS = ("@overload", "@hidden", "@section", "@contract", "@exposed", "@anchor")
GATHERED = ("Compile-time messages", "Exceptions", "Debug-build checks")
# A box heading's id spells an operator's symbol in words: the site's own ids drop the symbol, so that every operator
# on a page would be #operator, #operator_1, ... by order. A conversion function's id is its type's.
OPERATOR_WORDS = {
    "()": "call",
    "[]": "subscript",
    "->": "arrow",
    "*": "star",
    "/": "slash",
    "+": "plus",
    "-": "minus",
    "!": "not",
    "<<": "shift-left",
    "==": "eq",
    "!=": "ne",
    "<": "lt",
    "<=": "le",
    ">": "gt",
    ">=": "ge",
    "<=>": "spaceship",
    "+=": "plus-eq",
    "-=": "minus-eq",
    "*=": "star-eq",
    "/=": "slash-eq",
    "=": "assign",
}
ANCHOR = re.compile(r"[a-z0-9][a-z0-9_-]*")
# A field's name after the start of a line: a field that a reflow pulled into the paragraph above it.
FIELD_INSIDE = re.compile(r"(?<![`\w])(" + "|".join(FIELDS) + r"):")


# ---------------------------------------------------------------------------------------------------------------
# The entries


@dataclasses.dataclass
class Entry:
    decl: Decl
    qualified: str
    summary: str
    body: list[str]
    fields: dict[str, str]
    signatures: list[str]
    overloads: list["Entry"] = dataclasses.field(default_factory=list)  # @overloads with text: a box each
    members: list["Entry"] = dataclasses.field(default_factory=list)
    anchor: str = ""  # its heading's id: @anchor's, else the one its name gives


@dataclasses.dataclass
class Section:
    title: str
    headers: list[str]
    intro: list[str] = dataclasses.field(default_factory=list)
    intro_from: str = ""
    guide: str = ""
    entries: list[Entry] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class Contract:
    name: str
    sentence: str
    cases: list[tuple[str, str, str]] = dataclasses.field(default_factory=list)  # (case, expect, its file)
    named_by: list[str] = dataclasses.field(default_factory=list)
    where: tuple[Path, int] | None = None  # the header and line of its @contract


@dataclasses.dataclass
class Parts:
    markers: dict[str, str] = dataclasses.field(default_factory=dict)
    summary: str = ""
    body: list[str] = dataclasses.field(default_factory=list)
    fields: dict[str, str] = dataclasses.field(default_factory=dict)
    contracts: list[list] = dataclasses.field(default_factory=list)  # [name: sentence, line]


def split_doc(doc: Doc, path: Path, problems: Problems) -> Parts:
    """A block's markers, summary, body and fields. A field or a marker runs on to the next field, marker or blank
    line."""
    parts = Parts()
    paragraphs: list[list[str]] = [[]]
    running = None
    for number, line in enumerate(doc.lines, doc.line):
        if line.startswith("```"):
            problems.add(path, number, "a code block in a /// comment: code in use is the guide's")
        marker = next((m for m in MARKERS if line == m or line.startswith(m + " ")), None)
        field = next((f for f in FIELDS if line.startswith(f + ":")), None)
        for inside in FIELD_INSIDE.finditer(line, 1):
            problems.add(path, number, f"{inside.group(1)}: inside a line: a field starts a /// line of its own")
        if marker == "@contract":
            parts.contracts.append([line[len(marker) :].strip(), number])
            running = ("contract", len(parts.contracts) - 1)
        elif marker:
            if marker in parts.markers:
                problems.add(path, number, f"{marker} twice in one block")
            parts.markers[marker] = line[len(marker) :].strip()
            # These take no text, or a word on their own line.
            running = None if marker in ("@overload", "@exposed", "@anchor") else ("marker", marker)
        elif field:
            if field in parts.fields:
                problems.add(path, number, f"{field}: twice in one block")
            if parts.fields and FIELDS.index(list(parts.fields)[-1]) > FIELDS.index(field):
                problems.add(path, number, f"{field}: out of order - the fields go {', '.join(FIELDS)}")
            parts.fields[field] = line[len(field) + 1 :].strip()
            running = ("field", field)
        elif not line:
            if running and running[1] == "@section":
                parts.markers["@section"] += "\n"
            running = None if not (running and running[1] == "@section") else running
            if paragraphs[-1]:
                paragraphs.append([])
        elif running and running[0] == "field":
            parts.fields[running[1]] += " " + line.strip()
        elif running and running[0] == "contract":
            parts.contracts[running[1]][0] += " " + line.strip()
        elif running and running[1] == "@section":
            parts.markers["@section"] += "\n" + line
        elif running:
            parts.markers[running[1]] += " " + line.strip()
        else:
            if parts.fields:
                problems.add(path, number, "text after the fields: the fields end the block")
            paragraphs[-1].append(line)
    paragraphs = [p for p in paragraphs if p]
    if paragraphs:
        parts.summary = " ".join(line.strip() for line in paragraphs[0])
        parts.body = ["\n".join(p) for p in paragraphs[1:]]
    return parts


def read_sections(problems: Problems) -> list[Section]:
    sections = []
    if not SECTIONS.exists():
        return sections
    for number, line in enumerate(SECTIONS.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        if "|" not in line:
            problems.add(SECTIONS, number, "a section is a line 'Title | header, header'")
            continue
        title, headers = (part.strip() for part in line.split("|", 1))
        sections.append(Section(title, [h.strip() for h in headers.split(",") if h.strip()]))
    return sections


def read_cases() -> dict[str, tuple[str, str, Path, int]]:
    """Every contract_fail case: its name, the sentence its check stops on (`// expect:`, the line after its
    #ifdef), the contract it checks (`// contract:`, the line after that), its file and line."""
    cases = {}
    for folder in CONTRACT_CASES:
        for path in sorted(folder.glob("*.cpp")):
            lines = path.read_text(encoding="utf-8").splitlines() + ["", ""]
            for k, line in enumerate(lines):
                match = re.match(r"#ifdef DREAMOR_CASE_(\w+)", line)
                if match:
                    expect = lines[k + 1].removeprefix("// expect:").strip()
                    contract = (
                        lines[k + 2].removeprefix("// contract:").strip()
                        if lines[k + 2].startswith("// contract:")
                        else ""
                    )
                    cases[match.group(1)] = (expect, contract, path, k + 1)
    return cases


class Reference:
    def __init__(self, problems: Problems):
        self.problems = problems
        self.sections = read_sections(problems)
        self.by_title = {s.title: s for s in self.sections}
        self.contracts: dict[str, Contract] = {}
        self.messages: list[MessageSection] = read_messages(problems)
        self.mains: dict[tuple[str, str], list[Entry]] = {}
        self.classes: dict[str, Entry] = {}
        self.declared: list[Decl] = []
        self.opened: dict[str, set[str]] = {}
        overloads = []
        for path in sorted(INCLUDE.rglob("*.hpp")):
            overloads += self.read_header(path)
        for entry in overloads:
            self.attach_overload(entry, overloads)
        self.check_sections()
        self.assign_anchors()
        self.link_contracts()
        self.check_returns()

    def read_header(self, path: Path) -> list[Entry]:
        relative = str(path.relative_to(INCLUDE))
        scanner = Scanner(path, self.problems)
        self.declared += [d for d in scanner.decls if d.public]
        for doc in scanner.docs.values():
            self.problems.add(path, doc.line, "a /// block above no declaration a user names")
        events = sorted([(d.line, d) for d in scanner.loose] + [(d.line, d) for d in scanner.decls], key=lambda e: e[0])
        section, overloads = "", []
        for _, item in events:
            if isinstance(item, Doc):
                section = self.loose_block(path, relative, item, section)
                continue
            decl = item
            if not decl.public:
                exposed = decl.kind == "class" and decl.doc is not None and decl.doc.has("@exposed")
                if decl.doc and not exposed:
                    self.problems.add(path, decl.doc.line, f"a /// block on {decl.name}, which is not a user's")
                if not exposed:
                    continue
            if decl.doc is None:
                if not decl.owner or decl.owner in self.classes:
                    qualified = "::".join(filter(None, [decl.scope, decl.name]))
                    self.problems.add(path, decl.line, f"{decl.kind} {qualified} has no /// block")
                continue
            parts = split_doc(decl.doc, path, self.problems)
            self.add_contracts(path, parts)
            if "@hidden" in parts.markers:
                if not re.match(r"- \S", parts.markers["@hidden"]):
                    self.problems.add(path, decl.doc.line, "@hidden - <why a user never writes it>")
                continue
            if "@section" in parts.markers:
                self.problems.add(path, decl.doc.line, "@section goes in a block of its own")
            if not section:
                self.problems.add(path, decl.line, f"{decl.name} comes before any @section in its header")
                continue
            qualified = "::".join(filter(None, [decl.scope, decl.name]))
            entry = Entry(
                decl, qualified, parts.summary, parts.body, parts.fields, [decl.signature] if decl.signature else []
            )
            if "@anchor" in parts.markers:
                entry.anchor = parts.markers["@anchor"]
                if not ANCHOR.fullmatch(entry.anchor):
                    self.problems.add(path, decl.doc.line, "@anchor <id>: lower-case letters, digits, - and _")
            if "@overload" in parts.markers:
                if entry.anchor:
                    self.problems.add(
                        path, decl.doc.line, "@anchor on an @overload, whose box has no heading of its own"
                    )
                overloads.append(entry)
                continue
            if not entry.summary:
                self.problems.add(path, decl.doc.line, f"{decl.name}'s block has no summary sentence")
            self.mains.setdefault((decl.scope, decl.name), []).append(entry)
            if decl.kind == "class":
                self.classes[decl.where] = entry
            parent = self.classes.get(decl.owner) if decl.owner else None
            if decl.owner and parent is None:
                continue  # its class has no entry, which is named already
            if parent is not None:
                parent.members.append(entry)
            else:
                target = self.by_title.get(section)
                if target is None:
                    self.problems.add(path, decl.line, f"section {section!r} is not in {SECTIONS.name}")
                    continue
                target.entries.append(entry)
        return overloads

    def loose_block(self, path: Path, relative: str, doc: Doc, section: str) -> str:
        parts = split_doc(doc, path, self.problems)
        self.add_contracts(path, parts)
        if "@section" not in parts.markers:
            if not parts.contracts:
                self.problems.add(path, doc.line, "a /// block above no declaration")
            return section
        title, *intro = parts.markers["@section"].split("\n")
        title = title.strip()
        self.opened.setdefault(title, set()).add(relative)
        target = self.by_title.get(title)
        paragraphs = [p.strip("\n") for p in "\n".join(intro).split("\n\n") if p.strip()]
        paragraphs += ([parts.summary] if parts.summary else []) + parts.body
        if target is not None and paragraphs:
            if target.intro_from:
                self.problems.add(path, doc.line, f"{title} is introduced in {target.intro_from} already")
            target.intro_from = relative
            for paragraph in paragraphs:
                if paragraph.startswith("Guide:"):
                    target.guide = paragraph.removeprefix("Guide:").strip()
                else:
                    target.intro.append(paragraph)
        return title

    def add_contracts(self, path: Path, parts: Parts) -> None:
        for value, line in parts.contracts:
            match = re.match(r"(\w+):\s*(.+)", value, re.S)
            if not match:
                self.problems.add(path, line, "@contract <name>: <sentence>")
            elif match.group(1) in self.contracts:
                self.problems.add(path, line, f"@contract {match.group(1)} is defined twice")
            else:
                self.contracts[match.group(1)] = Contract(match.group(1), match.group(2).strip(), where=(path, line))

    def attach_overload(self, entry: Entry, overloads: list[Entry]) -> None:
        """An @overload with text joins the nearest main entry above it in its header, else the only one anywhere. One
        with no text is the same entry in another form (the && and const& forms of one step): its signature joins
        the nearest entry above it, main or @overload with text, whose form it repeats."""
        decl = entry.decl
        key = (decl.scope, decl.name)
        candidates = self.mains.get(key, [])
        textless = not has_text(entry)
        near = candidates + [o for o in overloads if textless and (o.decl.scope, o.decl.name) == key and has_text(o)]
        above = [e for e in near if e.decl.path == decl.path and e.decl.line < decl.line]
        if above:
            target = max(above, key=lambda e: e.decl.line)
        elif len(candidates) == 1:
            target = candidates[0]
        else:
            how = "no" if not candidates else "several"
            self.problems.add(decl.path, decl.line, f"@overload of {decl.name}: {how} main entries to join")
            return
        if textless:
            target.signatures += entry.signatures
            return
        if not entry.summary:
            self.problems.add(decl.path, decl.line, f"@overload of {decl.name} with fields: it needs a summary")
        target.overloads.append(entry)

    def check_sections(self) -> None:
        for title, headers in self.opened.items():
            target = self.by_title.get(title)
            if target is None:
                for header in headers:
                    self.problems.add(INCLUDE / header, 1, f"@section {title}: not in {SECTIONS.name}")
                continue
            for header in sorted(headers - set(target.headers)):
                self.problems.add(INCLUDE / header, 1, f"@section {title}: {SECTIONS.name} does not list this header")
        for section in self.sections:
            for header in section.headers:
                if header not in self.opened.get(section.title, set()):
                    self.problems.add(SECTIONS, 1, f"{section.title} lists {header}, which opens no @section of it")
            if section.title in GATHERED and section.entries:
                self.problems.add(SECTIONS, 1, f"{section.title} is gathered from the entries: none goes in it")
            where = INCLUDE / section.headers[0] if section.headers else SECTIONS
            if not section.intro:
                self.problems.add(where, 1, f"section {section.title} has no introduction after its @section")
            if not section.guide and section.title not in GATHERED:
                self.problems.add(where, 1, f"section {section.title} has no 'Guide: guide/<page>.md' line")
            rank = {h: k for k, h in enumerate(section.headers)}
            # By header, then the public entries before an @exposed class, whose header puts it first as a helper.
            section.entries.sort(
                key=lambda e: (rank.get(str(e.decl.path.relative_to(INCLUDE)), 0), "::internal::" in e.qualified)
            )

    def assign_anchors(self) -> None:
        """Each heading's id, which a link names: the one its name gives (anchor_of), unless @anchor names one. Two
        headings on one page whose names give one id are a problem until each names its own: otherwise the site
        would number them by order, and a new entry would take an old one's id, its links with it."""
        for section in self.sections:
            boxes = list(headed(section.entries))
            given = {}
            for entry in boxes:
                given[id(entry)] = anchor_of(entry.decl)
                if given[id(entry)] is None:
                    known = " ".join(OPERATOR_WORDS)
                    self.problems.add(
                        entry.decl.path,
                        entry.decl.line,
                        f"{entry.decl.name}: no word for it in OPERATOR_WORDS ({known})",
                    )
            counts = collections.Counter(given.values())
            named = set()  # the entries named as a problem already
            for entry in boxes:
                if entry.anchor or given[id(entry)] is None:
                    continue
                if counts[given[id(entry)]] > 1:
                    named.add(id(entry))
                    self.problems.add(
                        entry.decl.path,
                        entry.decl.line,
                        f"{entry.qualified}: another heading on {page_name(section.title)} has the id "
                        f"#{given[id(entry)]} too - name each one's with @anchor <id>",
                    )
                entry.anchor = given[id(entry)]
            seen: dict[str, Entry] = {}
            for entry in boxes:
                if entry.anchor == TITLE_ID:
                    self.problems.add(
                        entry.decl.path, entry.decl.line, f"#{TITLE_ID} is a page title's id: @anchor <id>"
                    )
                elif entry.anchor in seen and entry.anchor and id(entry) not in named:
                    self.problems.add(
                        entry.decl.path,
                        entry.decl.line,
                        f"{entry.qualified}: the id #{entry.anchor} is {seen[entry.anchor].qualified}'s already",
                    )
                seen.setdefault(entry.anchor, entry)

    def link_contracts(self) -> None:
        for name, (expect, contract, path, line) in read_cases().items():
            if not contract:
                self.problems.add(path, line, f"case {name} names no contract: // contract: <name>, after its expect")
            elif contract not in self.contracts:
                self.problems.add(path, line, f"case {name}: no @contract {contract}")
            else:
                self.contracts[contract].cases.append((name, expect, str(path.relative_to(ROOT))))
        for contract in self.contracts.values():
            if not contract.cases and contract.where:
                self.problems.add(*contract.where, f"@contract {contract.name} has no contract_fail case naming it")
        for entry in self.all_entries():
            for name in re.findall(r"\w+", entry.fields.get("Contract", "")):
                if name in self.contracts:
                    if entry.qualified not in self.contracts[name].named_by:
                        self.contracts[name].named_by.append(entry.qualified)
                else:
                    self.problems.add(entry.decl.path, entry.decl.line, f"Contract: {name} - no @contract {name}")

    def all_entries(self) -> list[Entry]:
        """Every entry, each followed by its @overloads with text."""
        return [
            one for section in self.sections for entry in headed(section.entries) for one in [entry, *entry.overloads]
        ]

    def check_returns(self) -> None:
        """A function that returns a value says what: its summary starts with Returns, or it has a Returns field."""
        for entry in self.all_entries():
            if entry.decl.kind not in ("function", "member function") or not entry.signatures:
                continue
            if entry.summary.startswith("Returns") or "Returns" in entry.fields:
                continue
            if returns_value(entry.signatures[0], entry.decl.name):
                self.problems.add(
                    entry.decl.path,
                    entry.decl.line,
                    f"{entry.decl.name} returns a value: its summary starts with Returns, or it has a Returns: field",
                )


def headed(entries: list[Entry]):
    """Each entry that has a heading of its own, then its members', depth first: the order of the page's boxes."""
    for entry in entries:
        yield entry
        yield from headed(entry.members)


def anchor_of(decl: Decl) -> str | None:
    """The id a heading's name gives: the name as the site's own ids write it (lower case), an operator's its symbol
    in words (OPERATOR_WORDS; None if the table has none), and a member's after its class's short name, so that
    members of two classes on one page do not share one (highsvar-index); a constructor's is <class>-constructor."""
    if decl.kind == "constructor":
        return decl.name.lower() + "-constructor"
    if not re.match(r"operator\b", decl.name):
        own = decl.name.lower()
    elif (symbol := decl.name.removeprefix("operator").strip()) in OPERATOR_WORDS:
        own = "operator-" + OPERATOR_WORDS[symbol]
    elif IDENT.match(symbol):  # a conversion function: its type
        own = "operator-" + "-".join(re.findall(r"\w+", symbol.lower()))
    else:
        return None
    return f"{decl.owner.rsplit('::', 1)[-1].lower()}-{own}" if decl.owner else own


def has_text(entry: Entry) -> bool:
    return bool(entry.summary or entry.body or entry.fields)


SPECIFIERS = {"static", "constexpr", "consteval", "inline", "explicit", "virtual", "friend", "extern"}


def returns_value(signature: str, name: str) -> bool:
    """Whether a function's signature returns something other than void; a conversion operator does."""
    _, rest = strip_template_header(signature)
    at = rest.find(name)
    words = [w for w in IDENT.findall(rest[: max(at, 0)]) if w not in SPECIFIERS]
    return words != ["void"]


def first_sentence(text: str) -> str:
    """A contract's text up to its first full stop: what an entry's Contract field shows."""
    match = re.match(r"(.+?\.)(\s|$)", text, re.S)
    return match.group(1) if match else text
