#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Measures what sets cost to compile, for the tables in docs/guide/cost.md.

    python3 tools/measure_compile_cost.py [--many] [compiler ...]     (default: g++-13 clang++-18)

Each measurement compiles one translation unit (-std=c++20 -O2 -c) against the Concert stub and reports CPU
time (user + sys, which a jumping wall clock - WSL - does not disturb) and peak memory, from /usr/bin/time. Each
compiler compiles each unit twice, as a debug build (the contract checks kept) and with -DNDEBUG, in the columns of
the tables.

- wide (the default): for 4, 16, 32 and 64 columns, one set of that many columns, filtered, grouped, with a
  VarTable on it and a sum over it.
- --many: for 1, 50 and 200 sets, that many sets of three columns of their own - each a template
  instantiation of its own, as a model's sets are - half a product of three ranges, half with a lateral list,
  each with a predicate, a VarTable and a sum.

It prints a Markdown table, then the compilers' versions.
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEAD = """#include <cstddef>
#include <vector>
#include <ilcplex/ilocplex.h>
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
"""


def wide(n: int) -> str:
    cols = ", ".join(f"C{i}" for i in range(n))
    half = ", ".join(f"C{i}" for i in range(n // 2))
    key = ", ".join(f"C{i}{{0}}" for i in range(n))
    decls = "\n".join(f"DREAMOR_VALUE_COLUMN(C{i}, std::size_t);" for i in range(n))
    return f"""{HEAD}{decls}
int main() {{
  const std::vector<std::size_t> v{{0, 1}};
  IloEnv env;
  const auto s = dreamor::index_set::relational::from<C0>(v){"".join(f".cross<C{i}>(v)" for i in range(1, n))}
                     .where([](C0 a, C{n - 1} b) {{ return a.get() <= b.get(); }})
                     .build();
  const auto g = s.groupBy<{half}>();
  const dreamor::concert::VarTable<{cols}> x{{env, s, 0, 1, ILOFLOAT, "x"}};
  IloExpr e = dreamor::concert::sumExpr(env, s, x).where([](C1 c) {{ return c > 0; }}).build();
  e.end();
  return static_cast<int>(g.size() + s.size()) + (x({key}).getUB() > 0 ? 0 : 1);
}}
"""


def many(n: int) -> str:
    decls, blocks = [], []
    for i in range(n):
        decls += [f"DREAMOR_VALUE_COLUMN(A{i}, std::size_t);", f"DREAMOR_VALUE_COLUMN(B{i}, std::size_t);"]
        decls += [f"DREAMOR_VALUE_COLUMN(C{i}, std::size_t);"]
        middle = f".lateral<B{i}>(next)" if i % 2 == 1 else f".cross<B{i}>(v)"
        blocks.append(f"""  {{
    const auto next = [&v](A{i}) -> const std::vector<std::size_t>& {{ return v; }};
    const auto keep = [](A{i} a, C{i} c) {{ return a.get() <= c.get(); }};
    const auto s = dreamor::index_set::relational::from<A{i}>(v){middle}.cross<C{i}>(v).where(keep).build();
    const dreamor::concert::VarTable<A{i}, B{i}, C{i}> x{{env, s, 0, 1, ILOFLOAT, "x{i}"}};
    IloExpr e = dreamor::concert::sumExpr(env, s, x).build();
    total += s.size() + next(A{i}{{0}}).size();
    e.end();
  }}""")
    body = "\n".join(blocks)
    return f"""{HEAD}{chr(10).join(decls)}
int main() {{
  const std::vector<std::size_t> v{{0, 1, 2}};
  IloEnv env;
  std::size_t total = 0;
{body}
  return static_cast<int>(total % 2);
}}
"""


def measure(compiler: str, flags: list[str], source: Path) -> tuple[float, int]:
    run = subprocess.run(
        ["/usr/bin/time", "-f", "%U %S %M", compiler, "-std=c++20", "-O2", *flags, "-c", f"-I{ROOT / 'include'}"]
        + [f"-I{ROOT / 'tests' / 'stub'}", str(source), "-o", "/dev/null"],
        capture_output=True,
        text=True,
    )
    if run.returncode != 0:
        sys.exit(f"{compiler} failed on {source.name}:\n{run.stderr}")
    user, system, peak_kb = run.stderr.strip().splitlines()[-1].split()
    return float(user) + float(system), int(peak_kb)


def version(compiler: str) -> str:
    return subprocess.run([compiler, "--version"], capture_output=True, text=True).stdout.splitlines()[0]


def main() -> None:
    parser = argparse.ArgumentParser(description="What sets cost to compile.")
    parser.add_argument("--many", action="store_true", help="many sets of three columns, not one wide set")
    parser.add_argument("compilers", nargs="*", default=["g++-13", "clang++-18"])
    args = parser.parse_args()
    sizes, make, what = ([1, 50, 200], many, "Sets") if args.many else ([4, 16, 32, 64], wide, "Columns")
    runs = [(c, flags) for c in args.compilers for flags in ([], ["-DNDEBUG"])]
    print(f"| {what} | " + " | ".join(c + (", NDEBUG" if flags else "") for c, flags in runs) + " |")
    print("|---|" + "---|" * len(runs))
    with tempfile.TemporaryDirectory() as tmp:
        for n in sizes:
            source = Path(tmp) / f"{what.lower()}_{n}.cpp"
            source.write_text(make(n))
            cells = [f"{s:.1f} s, {kb / 1024:.0f} MB" for s, kb in (measure(c, flags, source) for c, flags in runs)]
            print(f"| {n} | " + " | ".join(cells) + " |", flush=True)
    print()
    for compiler in args.compilers:
        print(f"- {compiler}: {version(compiler)}")


main()
