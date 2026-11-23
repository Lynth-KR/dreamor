#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""What the catalog's and the scenarios' build.py share: a benchmark's binaries, each built once, and once more for
each repetition with a code layout of its own (layout.py).

A build.py lists its binaries as Targets and calls build() from its own folder. build() reads the environment:

    CXX          the compiler: GCC 13 or later, or Clang 18 or later. The default is g++.
    JOBS         how many compiles run at once. The default is half the cores.
    LAYOUTS=n    also builds every binary for repetitions 0 to n-1, under build/layout/<rep>/. The default, 0, builds
                 none.
    LAYOUT_SEED  the seed the layouts follow from. The default, 20261025, is run.py's default --seed.
    LLD_DIR      the lld that links the repetitions' copies, which shuffles their sections. The default is LLVM 18's,
                 /usr/lib/llvm-18/bin.

A repetition's folder, build/layout/<rep>/, holds its record, layout.json, which layout.py writes and run.py checks
against its --seed, and pad.o, a padding whose size is the repetition's own. Each copy is compiled with every function
in a section of its own and linked with pad.o.
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field

from common.layout import write_record

FLAGS = ["-std=c++20", "-O2", "-DNDEBUG", "-march=native", "-Wall", "-Wextra"]


@dataclass
class Target:
    """A binary, build/<out>, compiled from `source` with FLAGS and `flags`, and linked with `libs`. Paths are
    relative to the benchmark's folder. tools/check_bench.py compiles the same source with the same `flags`."""

    out: str
    source: str
    flags: list[str]
    libs: list[str] = field(default_factory=list)


def setting(name: str, default: str) -> str:
    """An environment variable, or `default` when it is unset or empty."""
    return os.environ.get(name) or default


def make_layouts(cxx: str, reps: int) -> list[list[str]]:
    """Writes build/layout/<rep>/ for each repetition, and returns the options that compile and link its copies."""
    if reps == 0:
        return []
    seed = int(setting("LAYOUT_SEED", "20261025"))
    lld_dir = setting("LLD_DIR", "/usr/lib/llvm-18/bin")
    lld = os.path.join(lld_dir, "ld.lld")
    if not os.access(lld, os.X_OK):
        sys.exit(f"no ld.lld in {lld_dir}: install lld-18, or set LLD_DIR")
    options = []
    for rep in range(reps):
        folder = f"build/layout/{rep}"
        os.makedirs(folder, exist_ok=True)
        shuffle_seed, pad_bytes = write_record(seed, rep, folder, lld)
        pad = f"{folder}/pad.o"
        padding = f'\t.section .text.layout_pad,"ax",@progbits\n\t.space {pad_bytes}\n'
        subprocess.run([cxx, "-c", "-x", "assembler", "-", "-o", pad], input=padding, text=True, check=True)
        shuffle = f"-Wl,--shuffle-sections=.text*={shuffle_seed}"
        options.append(["-ffunction-sections", f"-B{lld_dir}", "-fuse-ld=lld", shuffle, pad])
    return options


def build(targets: list[Target]) -> int:
    """Builds every target into build/, and each repetition's copy into build/layout/<rep>/, JOBS compiles at a time;
    prints each compile's warnings under its output's name, and exits with status 1 if any compile failed. Returns
    the number of repetitions."""
    cxx = setting("CXX", "g++")
    jobs = int(setting("JOBS", str(max(1, (os.cpu_count() or 2) // 2))))
    reps = int(setting("LAYOUTS", "0"))
    os.makedirs("build", exist_ok=True)
    layouts = make_layouts(cxx, reps)
    # Each compile: its target, its output, and the options of its repetition's layout (none for the plain build).
    compiles = []
    for t in targets:
        compiles.append((t, f"build/{t.out}", []))
        compiles += [(t, f"build/layout/{rep}/{t.out}", options) for rep, options in enumerate(layouts)]

    def compile_one(job: tuple[Target, str, list[str]]) -> tuple[str, subprocess.CompletedProcess]:
        t, out, options = job
        command = [cxx, *FLAGS, *options, *t.flags, t.source, *t.libs, "-o", out]
        return out, subprocess.run(command, capture_output=True, text=True, check=False)

    failed = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for out, run in pool.map(compile_one, compiles):
            if run.stdout or run.stderr:
                print(f"--- {out}\n{run.stdout}{run.stderr}", end="", file=sys.stderr)
            if run.returncode != 0:
                failed.append(out)
    if failed:
        sys.exit("build failed: " + ", ".join(failed))
    return reps
