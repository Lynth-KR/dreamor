<div align="center">

<img src="assets/banner-light.svg#gh-light-mode-only" alt="dreamor — declarative relational extension for algebraic modeling in Operations Research" width="100%">
<img src="assets/banner-dark.svg#gh-dark-mode-only" alt="dreamor — declarative relational extension for algebraic modeling in Operations Research" width="100%">

<br>

<img src="assets/slogan-light.svg#gh-light-mode-only" alt="Model with dreamor. Thrive as a dreamer." width="80%">
<img src="assets/slogan-dark.svg#gh-dark-mode-only" alt="Model with dreamor. Thrive as a dreamer." width="80%">

<br>

[![C++20](https://img.shields.io/badge/C%2B%2B-20-FFD43B?style=flat-square&logo=cplusplus&logoColor=white&labelColor=1D2433)](#quick-start)
[![CPLEX Concert](https://img.shields.io/badge/backend-CPLEX%20Concert-FFD43B?style=flat-square&labelColor=1D2433)](#backends)
[![OR-Tools MathOpt](https://img.shields.io/badge/backend-OR--Tools%20MathOpt-FFD43B?style=flat-square&labelColor=1D2433)](#backends)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-FFD43B?style=flat-square&labelColor=1D2433)](LICENSE)

[Quick start](#quick-start)&emsp;[Building sets](docs/guide.md#building-sets--makeset)&emsp;[VarTable](docs/guide.md#variable-families--vartable)&emsp;[Sums](docs/guide.md#sums--sumof)&emsp;[OR-Tools samples](examples/mathopt_samples/README.md)&emsp;[Public API](docs/reference.md#public-api-reference)

**English** · [한국어](README.ko.md)

</div>

<br>

A thin modeling layer for optimization models in C++. You build **index sets** the way you declare sets in AMPL,
create a whole **variable family** over a set in one go, and group sets to build **sums**. Constraints and solving
use the solver's own API. The backends are CPLEX Concert and OR-Tools MathOpt (HiGHS, SCIP, Gurobi, Xpress, and
more).

```cpp
// set PRODUCE := {m in MACHINES, i in ITEMS[m], t in PERIODS};
const auto sPRODUCE = dreamor::makeSet<MachineCol, ItemCol, PeriodCol>(machines, items_of, periods).collect();

// var Yield {(m,i,t) in PRODUCE} >= 0, <= cap[m] / time[i];
const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};

// subject to Capacity {m in MACHINES, t in PERIODS}: sum {i: (m,i,t) in PRODUCE} time[i] * Yield[m,i,t] <= cap[m];
const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
}
```

To see how real model code changes, look at [three of OR-Tools' own MathOpt samples ported to
dreamor](examples/mathopt_samples/README.md), each next to the original.

---

## Documentation

| | |
|---|---|
| [Philosophy](docs/philosophy.md) | What dreamor holds to and why: its principles, the names a model uses, the rules of use, and what it leaves out on purpose |
| [Guide](docs/guide.md) | Every part in the order a model uses them: columns, entities, sets, grouping, variable families, sums, constraints, solutions |
| [API reference](docs/reference.md) | Every public type and function, in tables |
| [OR-Tools samples](examples/mathopt_samples/README.md) | Three of Google's MathOpt samples ported to dreamor, each beside the original |
| [Benchmarks](bench/README.md) | How dreamor's speed is measured against hand-written code, and the results |
| [Contributing](CONTRIBUTING.md) | Reporting a bug, proposing a feature, building and testing, and how a change gets in |
| [Changelog](CHANGELOG.md) | What changed in each release |

## Backends

The set layer (`dreamor/index_set.hpp`) depends on no solver. Only variable families (`VarTable`) and sums
(`sumOf`) use the solver's variable and expression types, so each backend has its own namespace and header, and
the header you include is your dependency.

| Backend | Header | Namespace | Solvers |
|---|---|---|---|
| IBM ILOG Concert | `dreamor/concert.hpp` | `dreamor::concert` | CPLEX |
| OR-Tools MathOpt | `dreamor/mathopt.hpp` | `dreamor::mathopt` | HiGHS, SCIP, GLOP, PDLP, CP-SAT (as MIP), GLPK, Gurobi, Xpress, and more |

Bounds, variable types, names, and solution reading take the solver API's own terms (for Concert: `IloEnv`,
`ILOFLOAT`, `IloCplex`). If the names get long, use an alias: `namespace concert = dreamor::concert;`. `ValueTable`
holds only values, so it is `dreamor::ValueTable` regardless of backend.

The same across backends: bounds given as numbers or column functions, variable names
`<name>_<key0>_<key1>...`, `std::out_of_range` when looking up a row that is not in the set (`vYield(row)`),
iteration in set order, and `sumOf`'s `where()`/`split()`.

Everything that differs in MathOpt is MathOpt's own notation.

```cpp
namespace math_opt = ::operations_research::math_opt;
math_opt::Model model("lot sizing");
const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
    model, sPRODUCE, 0, max_possible_qty, /*is_integer=*/false, "Yield"};   // Model& instead of env, is_integer for the kind
const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{model, sPRODUCE, 0, 1, true, "Setup"};

for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  const math_opt::LinearExpression totalLoad = dreamor::mathopt::sumOf(rows, machine_usage).build();  // no env
  model.AddLinearConstraint(totalLoad <= machine->capacity);
}

const auto result = math_opt::Solve(model, math_opt::SolverType::kHighs);
const auto yieldPlan = vYield.solution(*result);   // std::logic_error if there is no feasible solution
```

Semi-continuous variables exist only in Concert, because MathOpt models have none. MathOpt's `sumOf` accumulates
into a `LinearExpression`, so a term that returns a quadratic term (`QuadraticExpression`) does not compile
(Concert's `IloExpr` accepts quadratic terms too).

When building against the OR-Tools C++ prebuilt, define `OR_PROTO_DLL` as empty and link `libortools`,
`libprotobuf`, and the Abseil libraries (`tests/run_tests.sh --mathopt DIR` does exactly that).

## Install — CMake

It is a header-only library, so there is nothing to build; a single `dreamor::dreamor` target passes on the include
path and C++20. Link the solver yourself, next to it, according to the backend you use.

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --install build
```

```cmake
find_package(dreamor 0.2 CONFIG REQUIRED)
target_link_libraries(app PRIVATE dreamor::dreamor ortools::ortools)    # dreamor/mathopt.hpp
target_link_libraries(app PRIVATE dreamor::dreamor <CPLEX target>)       # dreamor/concert.hpp
```

The OR-Tools prebuilt ships its own CMake config, so `find_package(ortools CONFIG)` works and `ortools::ortools`
passes on the include path, the definition (`OR_PROTO_DLL`), and the libraries together (add the OR-Tools folder to
`CMAKE_PREFIX_PATH`). CPLEX has no official CMake config, so link it the way you already do. Pulling dreamor in
without installing, via `add_subdirectory(dreamor)` or `FetchContent`, gives you the same `dreamor::dreamor` (in that
case dreamor's files are not installed).

## Quick start

Multi-machine, multi-period lot sizing. Each machine can make a different subset of items (sparse), and periods are
linked by inventory.

```cpp
#include <format>
#include <ilcplex/ilocplex.h>
#include "dreamor/index_set.hpp"
#include "dreamor/concert.hpp"
struct Item { std::string name; double time, hold; std::vector<double> demand; };
struct Machine { std::string name; double capacity, setup; std::vector<const Item*> items; };

// 1) Name hooks next to the entities (the tokens that go into variable names)
inline const std::string& keyName(const Item* i) { return i->name; }
inline const std::string& keyName(const Machine* m) { return m->name; }

// 2) Columns
using MachineCol = dreamor::Col<const Machine*, "machine">;
using ItemCol    = dreamor::Col<const Item*, "item">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;

// 3) Sets
const auto items_of = [](MachineCol m) { return m->items; };     // dependent source: returns a range of pointers
const auto sPERIODS = dreamor::makeSet<PeriodCol>(periods).collect();
const auto sROUTES  = dreamor::makeSet<MachineCol, ItemCol>(machines, items_of).collect();
const auto sPRODUCE = dreamor::makeSet<PeriodCol>(sROUTES, periods).collect();     // expand an existing set
const auto sSTOCK   = dreamor::makeSet<ItemCol, PeriodCol>(items, periods).collect();

// 4) Variables
const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"};
const dreamor::concert::VarTable<ItemCol, PeriodCol> vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"};

const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };

// 5) Constraints
//    Inventory balance: for every (item, period). It must hold even when no machine makes the item in that
//    period, so it iterates sSTOCK
const auto orderedPeriods = sPERIODS.ordered();
const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
for (const auto& [i, t] : sSTOCK) {
  IloExpr totalInFlow = dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build();
  if (const auto* prevPeriod = orderedPeriods.prev(t)) { totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod)); }
  const double currentDemand = i->demand[t.get()];
  const auto endingStock = vStock(i, t);
  model.add(totalInFlow - currentDemand == endingStock);
  totalInFlow.end();
}
//    Machine capacity, each row named the way the variables are (Capacity_M0_0)
const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
  model.add(totalLoad <= machine->capacity).setName(std::format("Capacity_{}_{}", machine, period).c_str());
  totalLoad.end();
}
//    No production without setup
for (const auto& [row, yieldVar] : vYield) {
  model.add(yieldVar <= yieldVar.getUB() * vSetup(row));
}

// 6) Objective
IloExpr holdingCost = dreamor::concert::sumOf(env, sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
IloExpr setupCost = dreamor::concert::sumOf(env, sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) { return m->setup * vSetup(m, i, t); }).build();
IloExpr totalCost = holdingCost + setupCost;
model.add(IloMinimize(env, totalCost));
holdingCost.end();
setupCost.end();
totalCost.end();

// 7) Solve and read the solution
IloCplex cplex(model);
cplex.solve();
const auto yieldPlan = vYield.solution(cplex);
for (const auto& [row, qty] : yieldPlan) {
  if (qty > 1e-6) { print(std::get<MachineCol>(row), std::get<ItemCol>(row), std::get<PeriodCol>(row), qty); }
}
```

The full example lives in `examples/`. Data, columns, and sets are in a single solver-independent file,
`production_planning_sets.hpp`; on top of it sit the same model written for Concert, `production_planning.hpp` (run:
`production_planning.cpp`), and for MathOpt, `production_planning_mathopt.hpp` (run:
`production_planning_mathopt.cpp`, HiGHS). `tests/mathopt/test_cross_backend.cpp` checks that the two models build
the same constraints. A comparison version of the same model written with only `std::map` and Concert is
`production_planning_plain.hpp`.

[`examples/mathopt_samples/`](examples/mathopt_samples/README.md) has three of OR-Tools' own MathOpt samples
(`cocktail_hour`, `time_indexed_scheduling`, `lagrangian_relaxation`) ported to dreamor, with a walkthrough of
what changes in each.

## Utilities

Two headers dreamor uses itself, there for model code too. Neither depends on a solver.

| Header | |
|---|---|
| `dreamor/general/format_buffer.hpp` | `dreamor::general::FormatBuffer<N>` builds names in a fixed buffer, with no allocation per name. A model class inherits it privately and names rows with `format(...)`; a column prints as the token its variables' names use, so `model.add(load <= cap).setName(format("Capacity_{}_{}", m, t))` names a row the way `VarTable` names variables |
| `dreamor/logger/logger_light.hpp` | `DREAMOR_LOG_INFO` / `WARNING` / `ERROR` / `FATAL(fmt, args...)` write a line with the elapsed time, the call site and the level, the text through `std::format`, checked at compile time. `DREAMOR_DEBUG_*` vanish under `NDEBUG`, arguments and all. `dreamor::logger::FileSink sink{"model.log"};` sends the thread's lines to a file until the scope ends |

The [API reference](docs/reference.md#utilities) lists the rest.

## Benchmark

`bench/` compares dreamor with the same model code written by hand, as designed experiments in two
layers:

- `bench/catalog/`: each operation - lookups, grouping, set operations, slices, generation, order,
  entities - against hand-written arms (hash maps, direct arrays, sorting, `std::map`) over the
  shapes data takes and the ways models reach it, without a solver.
- `bench/scenario/`: whole models built end to end by dreamor and by hand, up to the moment the
  solver starts on them, through CPLEX and MathOpt: p-median, the model the modeling-language
  benchmarks use, and the lot-sizing example as written here, against hand-optimized code and
  against the usual `std::map` code (`examples/production_planning_plain.hpp`).

```bash
bench/scenario/build.sh
cd bench/scenario && python3 run.py --scenario lotsizing && python3 report.py ../results/<run>
```

Design, fairness conditions and how to read the results are in `bench/README.md`. Timing is not
run on CI.

## AI coding agents

**Agents writing models with dreamor** get `skills/dreamor/`. It is in the Agent Skills format (`SKILL.md`), so it
loads only when dreamor code is being written or fixed, and it carries the rules, the signatures, and every compile
error message with its fix. Copy the folder as a whole into the skills folder your agent reads (for Claude Code, the
project's `.claude/skills/dreamor/` or `~/.claude/skills/dreamor/`; other agents have their own skills folders).

**Agents working on this repository** read `AGENTS.md` at the root: how to test, the design rules, how to add a
diagnostic, and which documents change together. `CLAUDE.md` imports it with a single `@AGENTS.md` line so that
Claude Code reads the same file.

## Acknowledgments

Thanks to Claude, by Anthropic, for extensive help in building dreamor - implementation, tests, and tooling. The
design and the decisions behind it are the author's.

## License

[Apache License 2.0](LICENSE)
