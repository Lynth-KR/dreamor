#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measures what a wide set costs to compile, for the table in README.md / README.ko.md.

    python3 tools/measure_compile_cost.py [compiler ...]     (default: g++ clang++)

For 4, 16, 32, 63 and 64 columns it compiles one translation unit (-std=c++20 -O2 -c) that
builds a set of that many columns, filters it, groups it, puts a VarTable on it and sums over
it, against the Concert stub. It prints a Markdown table of CPU time (user + sys, which a
jumping wall clock - WSL - does not disturb) and peak memory, from /usr/bin/time.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLUMNS = [4, 16, 32, 63, 64]


def translation_unit(n: int) -> str:
    cols = ", ".join(f"C{i}" for i in range(n))
    half = ", ".join(f"C{i}" for i in range(n // 2))
    key = ", ".join(f"C{i}{{0}}" for i in range(n))
    decls = "\n".join(f'using C{i} = dreamor::Col<std::size_t, "c{i}">;' for i in range(n))
    return f"""#include <cstddef>
#include <vector>
#include <ilcplex/ilocplex.h>
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
{decls}
int main() {{
  const std::vector<std::size_t> v{{0, 1}};
  IloEnv env;
  const auto s = dreamor::makeSet<{cols}>({", ".join(["v"] * n)})
                     .where([](C0 a, C{n - 1} b) {{ return a.get() <= b.get(); }})
                     .collect();
  const auto g = s.groupBy<{half}>();
  const dreamor::concert::VarTable<{cols}> x{{env, s, 0, 1, ILOFLOAT, "x"}};
  IloExpr e = dreamor::concert::sumOf(env, s, x).where([](C1 c) {{ return c.get() > 0; }}).build();
  return static_cast<int>(g.size() + s.size()) + (x({key}).getUB() > 0 ? 0 : 1);
}}
"""


def measure(compiler: str, source: Path) -> tuple[float, int]:
    run = subprocess.run(
        [
            "/usr/bin/time",
            "-f",
            "%U %S %M",
            compiler,
            "-std=c++20",
            "-O2",
            "-c",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'tests' / 'stub'}",
            str(source),
            "-o",
            "/dev/null",
        ],
        capture_output=True,
        text=True,
    )
    if run.returncode != 0:
        sys.exit(f"{compiler} failed on {source.name}:\n{run.stderr}")
    user, system, peak_kb = run.stderr.strip().splitlines()[-1].split()
    return float(user) + float(system), int(peak_kb)


def version(compiler: str) -> str:
    return subprocess.run([compiler, "--version"], capture_output=True, text=True).stdout.splitlines()[0]


compilers = sys.argv[1:] or ["g++", "clang++"]
with tempfile.TemporaryDirectory() as tmp:
    results = {}
    for n in COLUMNS:
        source = Path(tmp) / f"columns_{n}.cpp"
        source.write_text(translation_unit(n))
        results[n] = [measure(compiler, source) for compiler in compilers]

print("| Columns | " + " | ".join(compilers) + " |")
print("|---|" + "---|" * len(compilers))
for n in COLUMNS:
    cells = [f"{seconds:.1f} s, {peak_kb / 1024:.0f} MB" for seconds, peak_kb in results[n]]
    print(f"| {n} | " + " | ".join(cells) + " |")
print()
for compiler in compilers:
    print(f"- {compiler}: {version(compiler)}")
