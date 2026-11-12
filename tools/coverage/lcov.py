#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The library's line coverage by the tests, as lcov files for Codecov, from a build of the coverage preset.

    cmake --workflow --preset coverage              # ORTOOLS_DIR set: build with coverage, run the tests
    python3 tools/coverage/lcov.py build/coverage           # an lcov file for each test program, in build/coverage/lcov/
    python3 tools/coverage/lcov.py build/coverage --summary # and each header's lines run, the programs added up
    python3 tools/coverage/lcov.py build/coverage --html    # and an HTML report to read, in build/coverage/html/

The preset builds with Clang's source-based coverage and runs the tests with LLVM_PROFILE_FILE in continuous mode
(%c), so that a program that stops on purpose - a contract_fail case aborts - keeps its counts. Each program is
exported on its own: the test files define types of one name differently (Item, Machine), so one export over every
program would meet records of one name with different code and drop some of them ("mismatched data"). Codecov adds
the programs' counts line by line, as --summary does. llvm-cov is told not to ask a debuginfod server about each
program (Ubuntu sets DEBUGINFOD_URLS), which takes minutes. A function that no test instantiates has no record, and
uninstantiated.py beside this finds those. The HTML report is one `llvm-cov show` over every program, each template
instance apart and the branches counted: it is for reading which lines and branches ran, and since it meets those
records of one name too, its totals can fall short of the lcov files'. docs/dev/testing.md, "Coverage", says what the figure covers and what
codecov.yml leaves out.
"""

import argparse
import collections
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
LIBRARY = ROOT / "include" / "dreamor"


def programs(build: Path) -> list[Path]:
    """The test programs: the executable files under the build's tests/, CMake's own files left out."""
    found = []
    for path in sorted((build / "tests").rglob("*")):
        if path.is_file() and os.access(path, os.X_OK) and "CMakeFiles" not in path.parts and path.suffix != ".so":
            with path.open("rb") as f:
                if f.read(4) == b"\x7fELF":
                    found.append(path)
    return found


def library_records(lcov: str) -> str:
    """The records of an lcov text whose source file is the library's. llvm-cov also writes a record for a test file
    where a library macro expands in it, which is the test's code, not the library's."""
    kept = []
    for record in lcov.split("end_of_record\n"):
        source = next((row[3:] for row in record.splitlines() if row.startswith("SF:")), None)
        if source is not None and Path(source).is_relative_to(LIBRARY):
            kept.append(record + "end_of_record\n")
    return "".join(kept)


def summary(lcov: Path) -> None:
    """Prints each header's lines run, a line counting as run if any program ran it."""
    lines: dict[str, dict[int, int]] = collections.defaultdict(dict)
    for info in sorted(lcov.glob("*.info")):
        source = None
        for row in info.read_text().splitlines():
            if row.startswith("SF:"):
                source = row[3:]
            elif row.startswith("DA:") and source:
                number, count = (int(x) for x in row[3:].split(",")[:2])
                lines[source][number] = max(lines[source].get(number, 0), count)
    total = run = 0
    for source in sorted(lines):
        counts = lines[source]
        done = sum(1 for c in counts.values() if c > 0)
        total, run = total + len(counts), run + done
        print(f"{Path(source).relative_to(LIBRARY)}: {done}/{len(counts)}")
    print(f"total: {run}/{total} lines, {100 * run / max(total, 1):.1f}%")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build", type=Path, help="the coverage preset's build folder")
    parser.add_argument("--llvm", default="-18", help="the LLVM tools' suffix, matching the compiler (default -18)")
    parser.add_argument("--summary", action="store_true", help="print each header's lines run")
    parser.add_argument("--html", action="store_true", help="also write an HTML report to read, in BUILD/html/")
    args = parser.parse_args()
    build = args.build.resolve()
    profiles = sorted((build / "profiles").glob("*.profraw"))
    merged = build / "coverage.profdata"
    if profiles:
        subprocess.run([f"llvm-profdata{args.llvm}", "merge", "-sparse", *profiles, "-o", merged], check=True)
        # Merged, the raw profiles are removed, so that the next run of the tests starts from none.
        for profile in profiles:
            profile.unlink()
    elif not merged.exists():
        print(f"lcov.py: no profiles in {build / 'profiles'} - run the coverage preset's tests", file=sys.stderr)
        return 1
    lcov = build / "lcov"
    lcov.mkdir(exist_ok=True)
    for old in lcov.glob("*.info"):
        old.unlink()

    def export(program: Path) -> None:
        command = [f"llvm-cov{args.llvm}", "export", "--debuginfod=false", "-format=lcov"]
        command += ["-instr-profile", str(merged), str(program), str(LIBRARY)]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        (lcov / f"{program.name}.info").write_text(library_records(result.stdout))

    found = programs(build)
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        list(pool.map(export, found))
    print(f"lcov.py: {len(found)} programs, {len(profiles)} new profiles, lcov files in {lcov}")
    if args.summary:
        summary(lcov)
    if args.html:
        html = build / "html"
        command = [f"llvm-cov{args.llvm}", "show", "--debuginfod=false", "-format=html", "-show-instantiations"]
        command += ["-show-branches=count", "-show-line-counts-or-regions", "-instr-profile", str(merged)]
        command += [str(found[0]), *(f"-object={program}" for program in found[1:]), "-output-dir", str(html)]
        # The records of one name with different code are reported on stderr, one line each: expected, as above.
        subprocess.run([*command, str(LIBRARY)], stderr=subprocess.DEVNULL, check=True)
        print(f"lcov.py: HTML report in {html / 'index.html'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
