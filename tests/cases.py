#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The compile_fail and contract_fail cases, and the syntax checks: what CTest runs one at a time.

A case is a block of a topic file - `#ifdef DREAMOR_CASE_<name>` to its `#endif` - built alone with
-DDREAMOR_CASE_<name>; its `// expect: <text>` is the line right after the #ifdef (tests/compile_fail/*.cpp).

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

import json
import re
import subprocess
import sys
from pathlib import Path

CASE = re.compile(r"#ifdef DREAMOR_CASE_(\w+)")
EXPECT = "// expect: "


def cases_of(folder: Path) -> list[tuple[str, Path]]:
    found = []
    for topic in sorted(folder.glob("*.cpp")):
        for line in topic.read_text().splitlines():
            m = CASE.fullmatch(line)
            if m:
                found.append((m.group(1), topic))
    return found


def expect_of(topic: Path, name: str) -> str | None:
    lines = topic.read_text().splitlines()
    at = lines.index(f"#ifdef DREAMOR_CASE_{name}")
    after = lines[at + 1] if at + 1 < len(lines) else ""
    return after[len(EXPECT) :] if after.startswith(EXPECT) else None


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
