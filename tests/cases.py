#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The compile_fail and contract_fail cases, and the syntax checks: what CTest runs one at a time.

A case is a block of a topic file - `#ifdef DREAMOR_CASE_<name>` to its `#endif` - built alone with
-DDREAMOR_CASE_<name>; its `// expect: <text>` is the line right after the #ifdef (tests/compile_fail/*.cpp). After it
come, where a case has them, `// contract: <name>` (a contract_fail case) and `// when:`, a line that is `//` alone and
`// fix:` (docs/dev/diagnostics.md). read_cases reads them, for this runner and for tools/gen_reference.py alike.

    cases.py list FOLDER                     every case of FOLDER's topic files: NAME<tab>FILE, a line each
    cases.py prepare CONFIG FOLDER           FOLDER's prelude precompiled, then its control.cc compiled:
                                             a control that fails makes the cases meaningless
    cases.py rejects CONFIG FOLDER FILE NAME the case must not compile, and the compiler's first error (with
                                             its notes, up to the next error) must say its expect line, if it has one
    cases.py compiles CONFIG FILE [FLAGS]    FILE must compile (a syntax check), with the strict flags or, FLAGS
                                             user, a user's (the examples)
    cases.py stops FILE NAME COMMAND...      COMMAND - the case's program - must stop, not exit 0, and print
                                             its expect line

CONFIG is the JSON tests/CMakeLists.txt writes: the compiler, whether it takes MSVC's options (cl.exe, clang-cl), its
flags for the cases (lenient), for the syntax checks (strict) and for the code users read (user), whether it reads
Clang's precompiled headers, and where they go. Under MSVC's options the prelude is not precompiled, and a case is
compiled /TP /Zs; cl.exe's first error is "error C<n>:", the others' "error:".
"""

import dataclasses
import json
import re
import subprocess
import sys
from pathlib import Path

CASE = re.compile(r"#ifdef DREAMOR_CASE_(\w+)")
EXPECT = "// expect: "


@dataclasses.dataclass
class Case:
    name: str
    topic: Path
    line: int  # its #ifdef's, counting from 1
    expect: str | None = None
    contract: str = ""
    when: str = ""
    fix: str = ""


def read_cases(topic: Path) -> tuple[list[Case], list[tuple[int, str]]]:
    """A topic file's cases, and what is wrong with their lines, as (line, text). A when runs on over the comment lines
    after it up to a line that is // alone; a fix runs on over the comment lines after it, up to the case's first line
    that is not a comment - a case's own comments come after its first line of code, or after a blank line."""
    lines = topic.read_text(encoding="utf-8").splitlines()
    cases, wrong = [], []
    for at, line in enumerate(lines):
        match = CASE.fullmatch(line)
        if not match:
            continue
        case, k = Case(match.group(1), topic, at + 1), at + 1
        if k < len(lines) and lines[k].startswith(EXPECT):
            case.expect, k = lines[k][len(EXPECT) :].strip(), k + 1
        if k < len(lines) and lines[k].startswith("// contract:"):
            case.contract, k = lines[k].removeprefix("// contract:").strip(), k + 1
        if k < len(lines) and lines[k].startswith("// when:"):
            when, k = [lines[k].removeprefix("// when:").strip()], k + 1
            while k < len(lines) and lines[k].startswith("// ") and not lines[k].startswith("// fix:"):
                when, k = [*when, lines[k].removeprefix("// ").strip()], k + 1
            if k < len(lines) and lines[k] == "//":
                k += 1
            else:
                wrong.append((k + 1, f"case {case.name}: a line that is // alone ends its when, before its fix"))
            fix = []
            if k < len(lines) and lines[k].startswith("// fix:"):
                fix, k = [lines[k].removeprefix("// fix:").strip()], k + 1
                while k < len(lines) and lines[k].startswith("// "):
                    fix, k = [*fix, lines[k].removeprefix("// ").strip()], k + 1
            else:
                wrong.append((k + 1, f"case {case.name}: a when with no fix after it"))
            case.when, case.fix = " ".join(when), " ".join(fix)
        elif k < len(lines) and lines[k].startswith("// fix:"):
            wrong.append((k + 1, f"case {case.name}: a fix with no when before it"))
        cases.append(case)
    return cases, wrong


def cases_of(folder: Path) -> list[tuple[str, Path]]:
    return [(case.name, topic) for topic in sorted(folder.glob("*.cpp")) for case in read_cases(topic)[0]]


def expect_of(topic: Path, name: str) -> str | None:
    return next(case.expect for case in read_cases(topic)[0] if case.name == name)


ERROR = re.compile(r"error( C\d+)?:")


def syntax_only(config: dict) -> list[str]:
    """The options that compile a C++ file without generating code."""
    return ["/TP", "/Zs"] if config.get("msvc") else ["-x", "c++", "-fsyntax-only"]


def first_error(output: str) -> str:
    """The compiler's first error and what follows it, up to the next error: what a user reads first."""
    lines = output.splitlines()
    starts = [i for i, line in enumerate(lines) if ERROR.search(line)]
    if not starts:
        return ""
    end = starts[1] if len(starts) > 1 else len(lines)
    return "\n".join(lines[starts[0] : end])


def pch(config: dict, folder: Path) -> tuple[Path, list[str]]:
    """Where FOLDER's precompiled prelude goes, and the flags that use it."""
    out = Path(config["pch_dir"]) / folder.as_posix().replace("/", "_")
    if config["clang_pch"]:
        return out / "prelude.pch", ["-include-pch", str(out / "prelude.pch")]
    return out / "prelude.hpp.gch", ["-include", str(out / "prelude.hpp")]


def run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True)


def prepare(config: dict, folder: Path) -> int:
    target, _ = pch(config, folder)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.unlink(missing_ok=True)
    if config.get("msvc"):
        return control(config, folder)
    made = run(
        [
            config["cxx"],
            *config["lenient"],
            f"-I{folder}",
            "-x",
            "c++-header",
            str(folder / "prelude.hpp"),
            "-o",
            str(target),
        ]
    )
    if made.returncode != 0:
        target.unlink(missing_ok=True)  # the cases then parse the prelude themselves
        print(f"the prelude did not precompile; the cases parse it each:\n{made.stderr}")
    return control(config, folder)


def control(config: dict, folder: Path) -> int:
    compiled = run(
        [config["cxx"], *config["lenient"], f"-I{folder}", *uses(config, folder), *syntax_only(config)]
        + [str(folder / "control.cc")]
    )
    if compiled.returncode != 0:
        print(
            f"{folder}/control.cc: FAILED to compile - the cases would be meaningless\n{compiled.stdout}{compiled.stderr}"
        )
        return 1
    return 0


def uses(config: dict, folder: Path) -> list[str]:
    target, flags = pch(config, folder)
    return flags if target.exists() else []


def rejects(config: dict, folder: Path, topic: Path, name: str) -> int:
    compiled = run(
        [
            config["cxx"],
            *config["lenient"],
            f"-I{folder}",
            *uses(config, folder),
            *syntax_only(config),
            f"-DDREAMOR_CASE_{name}",
            str(topic),
        ]
    )
    expect = expect_of(topic, name)
    output = compiled.stdout + compiled.stderr  # cl.exe writes its errors to standard output
    if compiled.returncode == 0:
        print(f"{name}: COMPILED but must not")
        return 1
    if expect is not None and expect not in first_error(output):
        print(f'{name}: rejected, but the first error does not say "{expect}"\n{output}')
        return 1
    print(f"{name}: rejected" + (" with its message" if expect is not None else ""))
    return 0


def compiles(config: dict, source: Path, flags: str = "strict") -> int:
    compiled = run([config["cxx"], *config[flags], *syntax_only(config), str(source)])
    if compiled.returncode != 0:
        print(f"{source}: SYNTAX FAILED\n{compiled.stdout}{compiled.stderr}")
        return 1
    return 0


def stops(topic: Path, name: str, command: list[str]) -> int:
    ran = subprocess.run(command, capture_output=True, text=True)
    output = ran.stdout + ran.stderr
    expect = expect_of(topic, name)
    if ran.returncode == 0:
        print(f"{name}: RAN TO THE END but must stop\n{output}")
        return 1
    if expect is not None and expect not in output:
        print(f'{name}: stopped, but without "{expect}"\n{output}')
        return 1
    print(f"{name}: stopped with its message")
    return 0


def main(argv: list[str]) -> int:
    verb, *args = argv
    if verb == "list":
        for name, topic in cases_of(Path(args[0])):
            print(f"{name}\t{topic.as_posix()}")  # forward slashes: CMake reads a backslash as an escape
        return 0
    if verb == "stops":
        return stops(Path(args[0]), args[1], args[2:])
    config = json.loads(Path(args[0]).read_text())
    if verb == "prepare":
        return prepare(config, Path(args[1]))
    if verb == "rejects":
        return rejects(config, Path(args[1]), Path(args[2]), args[3])
    if verb == "compiles":
        return compiles(config, Path(args[1]), *args[2:3])
    print(f"cases.py: no verb {verb}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
