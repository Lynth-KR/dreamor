<div align="center">

<img src="docs/images/banner-light.svg#gh-light-mode-only" alt="dreamor - declarative relational extension for algebraic modeling in Operations Research. Model with dreamor. Thrive as a dreamer." width="100%">
<img src="docs/images/banner-dark.svg#gh-dark-mode-only" alt="dreamor - declarative relational extension for algebraic modeling in Operations Research. Model with dreamor. Thrive as a dreamer." width="100%">

<br>

[![tests](https://img.shields.io/github/actions/workflow/status/Lynth-KR/dreamor/ci.yml?branch=main&label=tests&style=flat-square&labelColor=1D2433)](https://github.com/Lynth-KR/dreamor/actions/workflows/ci.yml)
[![codecov](https://img.shields.io/codecov/c/github/Lynth-KR/dreamor?label=codecov&style=flat-square&labelColor=1D2433)](https://codecov.io/gh/Lynth-KR/dreamor)
[![Release](https://img.shields.io/github/v/release/Lynth-KR/dreamor?label=release&color=FFD43B&style=flat-square&labelColor=1D2433)](https://github.com/Lynth-KR/dreamor/releases)
[![Docs](https://img.shields.io/badge/docs-site-FFD43B?style=flat-square&labelColor=1D2433)](https://lynth-kr.github.io/dreamor/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-FFD43B?style=flat-square&labelColor=1D2433&logo=cplusplus&logoColor=white)](#quick-start)
[![Platforms: Linux, Windows, macOS, WebAssembly](https://img.shields.io/badge/platforms-Linux%20%C2%B7%20Windows%20%C2%B7%20macOS%20%C2%B7%20WebAssembly-FFD43B?style=flat-square&labelColor=1D2433)](#install---cmake)
[![License: MPL 2.0](https://img.shields.io/badge/license-MPL%202.0-FFD43B?style=flat-square&labelColor=1D2433)](LICENSE)

[Quick start](#quick-start)&emsp;[Guide](https://lynth-kr.github.io/dreamor/guide/)&emsp;[API reference](https://lynth-kr.github.io/dreamor/reference/)&emsp;[OR-Tools samples](examples/mathopt_samples/README.md)&emsp;[Philosophy](https://lynth-kr.github.io/dreamor/philosophy/)


</div>

<br>

dreamor is a thin modeling layer for optimization models in C++. You declare **index sets** as relations over your
data, create a **variable family** over a set in one go, and build **sums** over sets and their groups. Constraints,
objectives and solving stay in the solver's own API. It is header-only C++20, and its backends are HiGHS, CPLEX
Concert and OR-Tools MathOpt.

```cpp
// what can be made, in set-builder notation: each machine, the items it makes, every period
const auto sPRODUCE =
    of<cMachine, cItem, cPeriod>(in<cMachine>(machines), in<cItem>(items_of), in<cPeriod>(sPERIODS)).build();
// what can be kept, as a chain: every item, every period
const auto sSTOCK = from<cItem>(items).cross(sPERIODS).build();

// how much is made, up to what the machine's hours allow
const hs::VarTable<cMachine, cItem, cPeriod> vMake{env, sPRODUCE, 0, max_make, HighsVarType::kContinuous, "Make"};

// capacity: the hours each machine works in a period
for (const auto& [m, t, rows] : sPRODUCE.groupBy<cMachine, cPeriod>()) {
  hs::addConstr(env, hs::sumExpr(rows, hours).build() <= m->capacity);
}
```

- **Sets are relations, in either notation.** Set-builder notation (`of`, `in`) or a chain of relational algebra
  (`from`, `cross`, `join`) builds the same sets; sets are derived, never changed.
- **Columns name the places in a row** and read as what they hold - `m->capacity`, `t + 1`, `vStock(i, t - 1)` -
  and a value never lands in another column's place.
- **A variable family is one constructor** over a set, with names that follow the key: `Make_M1_bolt_2`.
- **A mistake stops at compile time, in one sentence** that says what to write instead.
- **The solver stays the solver.** dreamor writes no constraint or objective of its own.

---

## Quick start

Lot sizing over a few periods: each machine makes some of the items, a setup costs, and stock carries demand from
one period to the next. This is the whole program, on HiGHS.

```cpp
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "dreamor/highs.hpp"

// The data: each item and machine once, by its primary key
struct Item {
  std::string name;
  double time, hold;           // hours a unit takes; the cost of holding a unit for a period
  std::vector<double> demand;  // a period's demand at [t - 1]
  [[nodiscard]] const std::string& primaryKey() const { return name; }
};
struct Machine {
  std::string name;
  double capacity, setup;                 // hours in a period; the cost of a setup
  std::vector<dreamor::Ref<Item>> items;  // the items it makes
  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

// The columns: the places of a set's rows, each named once
DREAMOR_ENTITY_COLUMN(cMachine, Machine);  // holds a Ref<Machine>
DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_VALUE_COLUMN(cPeriod, int);        // holds a period's number

namespace hs = dreamor::highs;

int main() {
  dreamor::EntityVector<Item> items;
  items.insert(Item{"bolt", 0.5, 0.1, {30, 0, 20}});
  items.insert(Item{"nut", 0.2, 0.1, {0, 40, 10}});
  items.freeze();
  dreamor::EntityVector<Machine> machines;
  machines.insert(Machine{"M1", 20.0, 5.0, {items.at("bolt"), items.at("nut")}});
  machines.insert(Machine{"M2", 8.0, 4.0, {items.at("nut")}});
  machines.freeze();
  const int T = 3;  // the last period

  // a set is written in set-builder notation or as a chain of relational algebra: this program writes both
  using dreamor::index_set::relational::from;
  using dreamor::index_set::set_builder::in;
  using dreamor::index_set::set_builder::of;

  // the periods, 1 to T
  const auto sPERIODS = of<cPeriod>(in<cPeriod>(dreamor::interval::closed(1, T))).build();
  // what can be made, in set-builder notation: each machine, the items it makes, every period
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };
  const auto sPRODUCE =
      of<cMachine, cItem, cPeriod>(in<cMachine>(machines), in<cItem>(items_of), in<cPeriod>(sPERIODS)).build();
  // what can be kept, as a chain: every item, every period
  const auto sSTOCK = from<cItem>(items).cross(sPERIODS).build();

  hs::HighsEnv env;
  // how much is made, whether the machine is set up for it, and how much is kept
  const auto max_make = [](cMachine m, cItem i) { return m->capacity / i->time; };
  const hs::VarTable<cMachine, cItem, cPeriod> vMake{env, sPRODUCE, 0, max_make, HighsVarType::kContinuous, "Make"};
  const hs::VarTable<cMachine, cItem, cPeriod> vSetup{env, sPRODUCE, 0, 1, HighsVarType::kInteger, "Setup"};
  const hs::VarTable<cItem, cPeriod> vStock{env, sSTOCK, 0, kHighsInf, HighsVarType::kContinuous, "Stock"};

  // balance: stock carried in + made - demand = stock carried out
  const auto madeByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();
  for (const auto& [i, t] : sSTOCK) {
    hs::HighsLinearExpression inflow = hs::sumExpr(madeByItemPeriod.slice(i, t), vMake).build();
    if (t > 1) { inflow += vStock(i, t - 1); }
    hs::addConstr(env, inflow - i->demand[t - 1] == vStock(i, t));
  }

  // capacity: the hours each machine works in a period
  const auto hours = [&](cMachine m, cItem i, cPeriod t) { return i->time * vMake(m, i, t); };
  for (const auto& [m, t, rows] : sPRODUCE.groupBy<cMachine, cPeriod>()) {
    hs::addConstr(env, hs::sumExpr(rows, hours).build() <= m->capacity);
  }

  // nothing is made without a setup
  for (const auto& [m, i, t] : sPRODUCE) {
    hs::addConstr(env, vMake(m, i, t) <= max_make(m, i) * vSetup(m, i, t));
  }

  // the cost: holding stock and setting up
  const auto holding = [&](cItem i, cPeriod t) { return i->hold * vStock(i, t); };
  const auto setups  = [&](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
  hs::minimize(env, hs::sumExpr(sSTOCK, holding).build() + hs::sumExpr(sPRODUCE, setups).build());

  // what each machine makes, period by period
  for (const auto& [row, made] : vMake.solution(env)) {
    const auto& [m, i, t] = row;
    if (made > 1e-6) { std::cout << std::format("{} makes {} {}s in period {}\n", m, made, i, t); }
  }
}
```

It prints, after HiGHS's log:

```text
M1 makes 30 bolts in period 1
M1 makes 20 bolts in period 3
M1 makes 50 nuts in period 2
```

A row is named as its variables are - `Make_M1_bolt_1` - and the machine prints by its primary key, `M1`. The same
model, with its data generated at any size and on each backend, is in [`examples/`](examples/).

The names this program types are all that a first model needs; the guide lists them, and the ones to learn next, in
[At a glance](https://lynth-kr.github.io/dreamor/guide/at-a-glance/).

## Backends

The set layer, `dreamor/index_set.hpp`, depends on no solver. Variable families (`VarTable`) and sums (`sumExpr`) use
the solver's variable and expression types, so each backend has a header and a namespace of its own, and the header
you include is your dependency.

| Backend | Header | Namespace | Solvers |
|---|---|---|---|
| HiGHS | `dreamor/highs.hpp` | `dreamor::highs` | HiGHS, through its own C++ API |
| IBM ILOG Concert | `dreamor/concert.hpp` | `dreamor::concert` | CPLEX |
| OR-Tools MathOpt | `dreamor/mathopt.hpp` | `dreamor::mathopt` | HiGHS, SCIP, GLOP, PDLP, CP-SAT (as MIP), GLPK, Gurobi, Xpress, and more |

The sets, the bounds, the variables' names, a lookup outside the set (`std::out_of_range`) and the sums' `where`
behave the same on every backend. What differs is each solver's own notation:

| | HiGHS | Concert | MathOpt |
|---|---|---|---|
| A variable family | `VarTable{env, sPRODUCE, 0, ub, HighsVarType::kContinuous, "Make"}` | `VarTable{env, sPRODUCE, 0, ub, ILOFLOAT, "Make"}` | `VarTable{model, sPRODUCE, 0, ub, false, "Make"}` |
| A sum | `sumExpr(rows, term).build()` | `sumExpr(env, rows, term).build()` | `sumExpr(rows, term).build()` |
| A constraint | `addConstr(env, load <= cap)` | `model.add(load <= cap)` | `model.AddLinearConstraint(load <= cap)` |
| Solving | `minimize(env, cost)` | `IloCplex cplex(model); cplex.solve();` | `Solve(model, SolverType::kHighs)` |
| The solution | `vMake.solution(env)` | `vMake.solution(cplex)` | `vMake.solution(*result)` |

HiGHS's C++ API is a matrix API, with no expressions to write a row in. `dreamor::highs` brings the interface of
highspy, HiGHS's own Python package, to C++: `HighsEnv` collects the model, and `addConstr`, `setObjective`,
`minimize` and `maximize` take expressions; everything else is HiGHS's own API on `env.highs()`. The
[guide](https://lynth-kr.github.io/dreamor/guide/backends/) says what each backend asks of a model.

## Install - CMake

dreamor is headers alone, so there is nothing to build: the `dreamor::dreamor` target passes on the include path and
C++20. Link the solver yourself, beside it.

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --install build
```

```cmake
find_package(dreamor 0.8 CONFIG REQUIRED)
target_link_libraries(app PRIVATE dreamor::dreamor highs::highs)        # dreamor/highs.hpp
target_link_libraries(app PRIVATE dreamor::dreamor <CPLEX target>)       # dreamor/concert.hpp
target_link_libraries(app PRIVATE dreamor::dreamor ortools::ortools)    # dreamor/mathopt.hpp
```

HiGHS and the OR-Tools prebuilt ship CMake configs of their own (`find_package(highs CONFIG)`,
`find_package(ortools CONFIG)`, with their folders on `CMAKE_PREFIX_PATH`); the OR-Tools prebuilt holds a HiGHS too.
CPLEX has none, so link it the way you already do. `add_subdirectory(dreamor)` or `FetchContent` gives the same
`dreamor::dreamor` without installing.

dreamor builds and passes its tests with GCC 13 and Clang 18 or newer on Linux, on libstdc++ or LLVM's libc++; with
Visual Studio 2022 17.14 and 2026, cl.exe and clang-cl, on Windows; with Apple Clang 17 (Xcode 16.4) on macOS; and with
Emscripten on WebAssembly. Its performance is measured and tuned on Linux x86-64 with libstdc++.

## Documentation

| | |
|---|---|
| [Guide](https://lynth-kr.github.io/dreamor/guide/) | How to do each thing a model does, a page a topic: entities, columns, sets, grouping and order, data on a set, variable families, sums, constraints, solutions |
| [API reference](https://lynth-kr.github.io/dreamor/reference/) | Every public type and function, a page a topic |
| [Philosophy](https://lynth-kr.github.io/dreamor/philosophy/) | What dreamor holds to and why, what it trusts you with, and what it leaves to you on purpose |
| [OR-Tools samples](examples/mathopt_samples/README.md) | Three of Google's MathOpt samples ported to dreamor, each beside the original |
| [Benchmarks](https://lynth-kr.github.io/dreamor/bench/) | dreamor against the same models written by hand: each operation over the shapes data takes, and whole models up to the solver's start |
| [Contributing](CONTRIBUTING.md) | Reporting a bug, proposing a feature, building and testing, and how a change gets in |
| [Releases](https://github.com/Lynth-KR/dreamor/releases) | What changed in each release |

## AI coding agents

**Writing models with dreamor:** copy `skills/dreamor/` into your agent's skills folder (for Claude Code,
`.claude/skills/` or `~/.claude/skills/`); it carries the guide, the reference, and every compile-time message with
its fix. **Working on dreamor:** agents read [`AGENTS.md`](AGENTS.md), which `CLAUDE.md` imports.

## Acknowledgments

Thanks to Claude, by Anthropic, for extensive help in building dreamor - implementation, tests, and tooling. The
design and the decisions behind it are the author's.

## License

The library is under the [Mozilla Public License 2.0](LICENSE): a file of dreamor that you change stays under the
MPL, and your own files, and the program that uses dreamor, are yours to license. The code you are meant to copy -
the examples, the guide's code, every code block in a document - is [MIT-0](LICENSES/MIT-0.txt); the documents' text
is [CC-BY-4.0](LICENSES/CC-BY-4.0.txt); the banner is dreamor's own artwork ([NOTICE](NOTICE)). Each file's license
is in its first lines or in [REUSE.toml](REUSE.toml). Releases up to 0.5.0 were under the Apache License 2.0.
