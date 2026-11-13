# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The compile-time messages, read from the compile_fail cases: a case's `// expect:` is the message its compiler
must give first, and one case of each message says when it comes and what to write instead, in `// when:` and
`// fix:` (docs/dev/diagnostics.md). A case with no message, which C++ itself rejects, says the same.

    #ifdef DREAMOR_CASE_<name>
    // expect: <the message's words>
    // when: <what the code does>        - runs on over the next // lines, as the formatter wraps it,
    //                                     up to a line that is // alone
    // fix: <what to write instead>      - runs on up to the case's first line that is not a comment

A message's explanation is written once, on one of the cases that expect it; the others name the message alone.

The messages' whole sentences come from the headers: every static_assert's message, a string literal or a
DREAMOR_INTERNAL_..._MESSAGE macro, goes under the expect it contains - the longest, where several do - and one that no
expect contains is a message no case pins, which fails, unless INTERNAL names it.
"""

import dataclasses
import re
from pathlib import Path

from refgen.common import INCLUDE, ROOT, Problems
from refgen.common import cases as case_reader

Case = case_reader.Case

TESTS = ROOT / "tests"
# The page's sections, in reading order, and the topic files of compile_fail cases that each gathers. Every topic
# file of the three folders is in one section.
SECTIONS = (
    ("Entities", ["compile_fail/entity_vector.cpp"]),
    ("Columns", ["compile_fail/columns.cpp"]),
    ("Sets from records", ["compile_fail/records.cpp"]),
    ("Chains", ["compile_fail/chain.cpp"]),
    ("Set-builder notation", ["compile_fail/set_builder.cpp"]),
    ("Conditions and column callables", ["compile_fail/where.cpp"]),
    ("Sets", ["compile_fail/set.cpp", "compile_fail/set_too_many_columns.cpp"]),
    ("Grouped", ["compile_fail/grouped.cpp"]),
    ("Ordered", ["compile_fail/ordered.cpp"]),
    ("Intervals", ["compile_fail/interval.cpp"]),
    ("Values over a set", ["compile_fail/aggregate.cpp"]),
    ("VarTable", ["compile_fail/vartable.cpp"]),
    ("Sums", ["compile_fail/sumexpr.cpp"]),
    ("ParamTable and SolutionTable", ["compile_fail/tables.cpp"]),
    (
        "dreamor::highs",
        ["highs/compile_fail/rows.cpp", "highs/compile_fail/sumexpr.cpp", "highs/compile_fail/vartable.cpp"],
    ),
    ("dreamor::mathopt", ["mathopt/compile_fail/sumexpr.cpp", "mathopt/compile_fail/vartable.cpp"]),
)
FOLDERS = ("compile_fail", "mathopt/compile_fail", "highs/compile_fail")
# The static_asserts that no model can reach, which no case can pin: checks of dreamor's own code, each a promise the
# public checks before it keep. A message's words, and why no model meets it.
INTERNAL = {
    "AlignedAllocator: the alignment is a power of two, at least the element's own.": "an allocator of dreamor's own",
    "Sources and columns disagree.": "the set engine, after the chain's checks of its sources and columns",
    "The sources' columns and the set's are not the same columns.": "the set engine, after the chain's checks",
    "This source reads a column that is not bound when it runs.": "the set engine, after the class-scope checks",
}
LITERAL = r'"(?:\\.|[^"\\\n])*"'


@dataclasses.dataclass
class Message:
    """A message and its explanation, or, with no text, a case that C++ rejects with an error of its own."""

    text: str | None
    when: str
    fix: str
    cases: list[Case] = dataclasses.field(default_factory=list)
    sentences: list[str] = dataclasses.field(default_factory=list)  # the whole messages, from the headers


@dataclasses.dataclass
class MessageSection:
    title: str
    messages: list[Message] = dataclasses.field(default_factory=list)


def joined(run: str) -> str:
    """Adjacent string literals as the one string the compiler makes of them."""
    return "".join(re.findall(r'"((?:\\.|[^"\\\n])*)"', run)).replace('\\"', '"')


def header_sentences() -> list[tuple[str, Path, int]]:
    """Every static_assert message in the headers, with its header and line: a string literal, or a
    DREAMOR_INTERNAL_..._MESSAGE macro's literal."""
    macros, found = {}, []
    for path in sorted(INCLUDE.rglob("*.hpp")):
        text = re.sub(r"\\\n", " \n", path.read_text(encoding="utf-8"))  # keeps the lines a message is reported at
        text = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)
        for m in re.finditer(rf"#define (DREAMOR_INTERNAL_\w+)\s+((?:{LITERAL}\s*)+)", text):
            macros[m.group(1)] = joined(m.group(2))
        for m in re.finditer(r"\bstatic_assert\s*\(", text):
            k, depth = m.end(), 1
            while depth and k < len(text):
                if text[k] == '"':
                    k = re.compile(LITERAL).match(text, k).end()
                    continue
                depth += (text[k] == "(") - (text[k] == ")")
                k += 1
            inner = text[m.end() : k - 1]
            line = text[: m.start()].count("\n") + 1
            run = re.search(rf"((?:{LITERAL}\s*)+)\s*$", inner)
            macro = re.search(r"(DREAMOR_INTERNAL_\w+_MESSAGE)\s*$", inner)
            if run:
                found.append((joined(run.group(1)), path, line))
            elif macro:
                found.append((macro.group(1), path, line))
    return [(macros.get(s, s), path, line) for s, path, line in found]


def read_messages(problems: Problems) -> list[MessageSection]:
    """The page's sections, each with its messages in the order of their first case, and the checks: every topic file
    in a section, every message explained once, every case with no message explained."""
    cases: list[Case] = []
    for folder in FOLDERS:
        for path in sorted((TESTS / folder).glob("*.cpp")):
            found, wrong = case_reader.read_cases(path)
            cases += found
            for line, text in wrong:
                problems.add(path, line, text)
    section_of = {}
    for title, files in SECTIONS:
        for file in files:
            if not (TESTS / file).is_file():
                problems.add(Path(__file__), 1, f"section {title} lists {file}, which is not a topic file of cases")
            section_of[TESTS / file] = title
    for path in sorted({c.topic for c in cases} - set(section_of)):
        problems.add(path, 1, "a topic file of compile_fail cases in no section of refgen/messages.py")
    explained: dict[str, Case] = {}
    for case in cases:
        if case.expect is None:
            if not case.when:
                problems.add(case.topic, case.line, f"case {case.name} gives no message: say when and fix")
        elif case.when:
            if case.expect in explained:
                first = explained[case.expect]
                problems.add(case.topic, case.line, f"case {case.name}: its message is explained by {first.name} too")
            else:
                explained[case.expect] = case
    sections = {title: MessageSection(title) for title, _ in SECTIONS}
    messages: dict[str, Message] = {}
    for case in cases:
        title = section_of.get(case.topic)
        if case.expect is None:
            if case.when and title:
                sections[title].messages.append(Message(None, case.when, case.fix, [case]))
            continue
        message = messages.get(case.expect)
        if message is None:
            source = explained.get(case.expect)
            if source is None:
                problems.add(case.topic, case.line, f"case {case.name}: no case explains its message - when and fix")
                continue
            message = Message(case.expect, source.when, source.fix)
            messages[case.expect] = message
            home = section_of.get(source.topic)
            if home:
                sections[home].messages.append(message)
        message.cases.append(case)
    expects = list(messages)
    for sentence, path, line in header_sentences():
        within = [e for e in expects if e in sentence]
        if within:
            message = messages[max(within, key=len)]
            if sentence not in message.sentences:
                message.sentences.append(sentence)
        elif sentence not in INTERNAL:
            problems.add(path, line, f"a static_assert message no compile_fail case expects: {sentence[:60]}...")
    return [s for s in sections.values() if s.messages]
