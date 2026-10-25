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

[Quick start](#quick-start)&emsp;[Building sets](#building-sets--makeset)&emsp;[VarTable](#variable-families--vartable)&emsp;[Sums](#sums--sumof)&emsp;[OR-Tools samples](examples/mathopt_samples/README.md)&emsp;[Public API](#public-api-reference)

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
for (const auto& [machine, period, rows] : producedByMachinePeriod)
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
```

To see how real model code changes, look at [three of OR-Tools' own MathOpt samples ported to
dreamor](examples/mathopt_samples/README.md), each next to the original.

---

## Contents

- [Contents](#contents)
- [Design principles](#design-principles)
- [Naming](#naming)
- [Backends](#backends)
- [Install — CMake](#install--cmake)
- [Quick start](#quick-start)
- [Columns](#columns)
- [Entities — EntityVector](#entities--entityvector)
- [Building sets — makeSet](#building-sets--makeset)
- [Deriving sets from sets](#deriving-sets-from-sets)
- [Grouping — groupBy](#grouping--groupby)
- [Variable families — VarTable](#variable-families--vartable)
- [Sums — sumOf](#sums--sumof)
- [Constraints](#constraints)
- [Reading solutions](#reading-solutions)
- [Concert resources and end()](#concert-resources-and-end)
- [Rules](#rules)
- [What dreamor does not do](#what-dreamor-does-not-do)
- [Public API reference](#public-api-reference)
- [Tests and development environment](#tests-and-development-environment)
- [Benchmark](#benchmark)
- [AI coding agents](#ai-coding-agents)
- [Acknowledgments](#acknowledgments)
- [License](#license)

---

## Design principles

- **Columns are called by name.** You refer to a column by its column type, such as `MachineCol` or `PeriodCol`,
  not by its position in a tuple. Predicates, bound functions, and the terms of a sum are all callables that
  **take the columns they read as parameters**.
- **A set is a fact.** Once built, a set cannot be changed (it is read-only). When you need another set, you derive
  a new one with `where`, `select`, and the like.
- **One VarTable per variable family.** Which variables exist is decided by that table's set. The VarTable
  constructor is the only place variables are created.
- **Clarity is not traded for performance.** Performance work is done only where it adds no concept to the user
  API (internal hashing, set sharing, and so on).
- **One tool for each job.** Each tool has one job, and no two tools share one - which is also why there is no
  second syntax for the same thing (pipe operators, free-function aliases, and so on). Combining tools is never
  restricted, though. When tools are orthogonal, different combinations reaching the same result is natural, and
  either way is right: `makeSet<>(sORDER_ITEMS, sPROMO)` and `sORDER_ITEMS.where(sPROMO.membership())` give the
  same rows.

## Naming

Sets and variable families have a fixed name shape: the AMPL model's name with one prefix in front, so the AMPL
comments and the code match once the prefix is dropped.

| What | Shape | Example | AMPL |
|---|---|---|---|
| Column type | PascalCase + `Col` | `MachineCol`, `PeriodCol` | |
| Set (`SetOf`) | `s` + SCREAMING_SNAKE_CASE | `sPRODUCE`, `sPEAK_PRODUCE` | `PRODUCE` |
| Variable family (`VarTable`) | `v` + PascalCase | `vYield`, `vSetup`, `vStock` | `Yield` |

Sets get a prefix because an all-caps name in C++ looks like a macro or a constant (`ILOFLOAT`, `ILOBOOL`).

Other names (callables such as predicates and terms, expressions, groupings, solution tables) are not prescribed.
The examples in this document write callables that take columns in snake_case (`items_of`, `machine_usage`) and
everything else in camelCase (`totalInFlow`, `yieldPlan`).

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
find_package(dreamor 0.1 CONFIG REQUIRED)
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
  if (const auto* prevPeriod = orderedPeriods.prev(t)) totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
  const double currentDemand = i->demand[t.get()];
  const auto endingStock = vStock(i, t);
  model.add(totalInFlow - currentDemand == endingStock);
  totalInFlow.end();
}
//    Machine capacity
const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
  model.add(totalLoad <= machine->capacity);
  totalLoad.end();
}
//    No production without setup
for (const auto& [row, yieldVar] : vYield) model.add(yieldVar <= yieldVar.getUB() * vSetup(row));

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
for (const auto& [row, qty] : yieldPlan)
  if (qty > 1e-6) print(std::get<MachineCol>(row), std::get<ItemCol>(row), std::get<PeriodCol>(row), qty);
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

## Columns

```cpp
using MachineCol = dreamor::Col<const Machine*, "machine">;   // value type + name
using PeriodCol  = dreamor::Col<std::size_t, "period">;       // the value may be an integer
```

- A `Col` converts implicitly to the value it wraps, and if that value is a pointer, `->` and `*` reach the entity
  directly (`m->capacity`). Get the value out with `t.get()`.
- One entity type may have more than one column. Different names make different columns (`FromCol` and `ToCol`,
  both `const Node*`).
- **Tokens for variable names**: for an entity-pointer column, a `keyName(const T*)` function placed in the **same
  namespace** as the entity is used, or else the entity's `primaryKey()` (next section; a debug build says so once
  per column). Values printable with `std::format` (integers, strings) need no hook.
- **Lookups by key take column values only.** `vYield(m, i, t)`, `yieldPlan(m, i, t)`, `slice(i, t)`, `prev(t)`, and
  `in_routes(m, i)` take the key's columns in declaration order, or one row of the set. Even though `Col` converts
  implicitly from its value, bare values (`x(2, 1)`, a bare pointer) and braces (`slice({i, t})`) are not accepted;
  compilation stops with one sentence telling you what to change. This keeps two int-valued columns from silently
  passing in swapped positions. If you start from a value, wrap it: `PeriodCol{2}`.

## Entities — EntityVector

A set keyed by entity pointers counts on each entity being one object: two objects for one machine are two machines
to every set built on them. `dreamor::EntityVector<T>` holds each entity once, by the primary key the entity
declares next to its type, as it does `keyName`:

```cpp
inline const std::string& primaryKey(const Machine& m) { return m.name; }     // found by ADL

dreamor::EntityVector<Machine> plant;
const auto [first, inserted] = plant.emplace(Machine{"M1", 40.0, 5.0, {}});   // inserted == false: M1 was there
const auto sRUN = dreamor::makeSet<MachineCol, PeriodCol>(plant, periods).collect();
plant.modify(&*first, [](Machine& m) { m.setup = 4.0; });                     // the one way to change one
```

- It reads like `std::vector` - indexing, iteration in insertion order, `size()` - and inserts like `std::set`:
  `insert` and `emplace` return `{the element with that key, whether it went in}`; `find(key)`, `contains(key)`.
- Elements never move (a `std::deque` underneath) and nothing is erased, so pointers into the model stay the
  entity's. Copying is deleted - a copy would be a second object for every entity; moving keeps each in place.
- Access is const. `modify(entity, fn)` changes one in place, and its primary key must come out the same (a debug
  build checks). Sets are snapshots: a set or a model already built is not recomputed, so `modify` is for writing
  results back afterwards - which usually belong in a `ValueTable`.
- A composite key is a `std::tuple`: `return std::tuple{r.plant, r.code};`.
- It is a choice, not a requirement: any container that keeps the contract below is a source.

## Building sets — makeSet

```cpp
const auto sNAME = dreamor::makeSet<Cols...>(sources...).where(predicate).where(predicate)...collect();
```

Each column of `makeSet<A, B, C>` corresponds to one source. There are three kinds of source.

| Source | Example | Meaning |
|---|---|---|
| Container | `machines` | Every value of that column. The elements may be pointers (`std::vector<const Machine*>`), the entities themselves (`std::vector<Machine>`), or smart pointers (`std::vector<std::unique_ptr<Machine>>`); any of them yields a pointer column |
| Dependent source | `[](MachineCol m) { return m->items; }` | Values that depend on columns already fixed earlier |
| Existing set | `makeSet<PeriodCol>(sROUTES, periods)` | Fills all of that set's columns at once. Two sets that share a column are joined on it |

- **AMPL correspondence**
  - `{m in MACHINES, i in ITEMS[m]}` → `makeSet<MachineCol, ItemCol>(machines, items_of).collect()`
  - `{(m,i) in ROUTES, t in PERIODS: open[i,t]}` → `makeSet<PeriodCol>(sROUTES, periods).where(is_open).collect()`
  - `{(m,i) in ROUTES, (i,t) in OPEN}` → `makeSet<>(sROUTES, sOPEN).collect()`
- **Set sources that share a column are joined on it** (a natural join). The one visited second is looked up by the
  shared columns instead of scanned, so the product is never built: the cost is the two sets plus the result. A set
  whose columns all lie in another one filters it, with the same rows as `sROUTES.where(sOPEN.membership())`. A
  column named in `makeSet<...>` is always new; repeating a set source's column there is a compile error.
- **A set has at most 64 columns**, the columns of a set source included: while the visit order is planned, a
  set's columns are bits of one 64-bit mask. A 65th stops at compile time on dreamor's own message.
- **A predicate takes only the columns it reads.** `[](ItemCol i, PeriodCol t)` is evaluated as soon as i and t are
  fixed, and if it fails, the whole loop below is skipped. Parameter order and `.where()` order are free.
- A dependent source must return a **range of pointers to entities** (`std::vector<const Item*>`, a span, a view),
  not the entities themselves.
- A container source must be an lvalue (no temporary vectors).
- **No source holds an element twice**: a container, the range a dependent source returns, a set. An entity is one
  object, held by one container, so two objects for one machine are a repeat too (`EntityVector` keeps that for
  entities). This is a precondition, as in the standard library: breaking it is undefined behavior, and a debug build
  (no `NDEBUG`) stops on it with a message naming the source. In return, a set never holds a row twice - no
  operation below makes a repeat - and each key of a VarTable has exactly one variable.
- Rows come out in generation order, and which column varies fastest is not fixed. If order matters, sort with
  `orderBy`.
- To spell out a set's type, use `dreamor::SetOf<MachineCol, ItemCol>`.

## Deriving sets from sets

A set is read-only (`begin`/`end`/`size`, range-for, and read-only algorithms). Instead you **derive**. Copying is
O(1) and shares the rows.

| Member | What it does | AMPL |
|---|---|---|
| `S.where(p, q)` | Rows that pass every predicate | `{(m,i) in S: p and q}` |
| `S.select<A, B>()` | Column projection (duplicates removed). Picking every column in a different order reorders the columns | `setof {(m,i,t) in S} (m,i)` |
| `S.select(expr)` | Each row as `expr` makes it (duplicates removed) | `setof {(i,j) in S} (j,i)` |
| `S.orderBy(key)` | Stable sort by `key(row)` | |
| `S.groupBy<A>()` | Grouping (next section) | slices |
| `S.membership()` | O(1) membership predicate, to put in another set's `where` | `(m,i) in S` |
| `S.ordered()` | ord / next / prev / nextCyclic / prevCyclic | ordered set |

| Free function | |
|---|---|
| `dreamor::unite(A, B)` | A ∪ B |
| `dreamor::intersect(A, B)` | A ∩ B |
| `dreamor::difference(A, B)` | A \ B |

```cpp
const auto sPEAK_PRODUCE  = sPRODUCE.where(is_peak);                  // production rows in peak-demand periods
const auto sMACHINE_ITEM  = sPRODUCE.select<MachineCol, ItemCol>();  // (machine, item) only
const auto in_routes      = sROUTES.membership();
const auto orderedPeriods = sPERIODS.ordered();
if (const auto* prevPeriod = orderedPeriods.prev(t)) { /* the previous period */ }
```

`ordered()` is a positional view (`Ordered`) over the deduplicated set. Positions count from 0. `next`/`prev`/
`nextCyclic`/`prevCyclic` may take a step count after the element (`next(t, 2)`, negative allowed) and return a
pointer to the row, or `nullptr` past the boundary or for an element not in the set. The rest is in the
[public API reference](#public-api-reference).

The `Membership` returned by `membership()` is a predicate object, so you may also call it directly, passing the
columns separately (`in_routes(MachineCol{m}, ItemCol{i})`) or a single row tuple (`in_routes(row)`).

`select(expr)` is the general `setof`. `expr` takes the columns it reads, as a predicate does, and returns the new
row's columns: one `Col`, or a `std::tuple` of them. A value moves into another column only where that column is
written out. `ToCol` does not turn into `FromCol` on its own, so two columns of the same value type cannot trade
places unnoticed.

```cpp
// (j,i) for every (i,j)
const auto sREVERSED = sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; });
const auto sTAILS    = sNODES.select([](NodeCol n) { return FromCol{n}; });  // the nodes as arc tails
```

The key of `orderBy` takes the whole row, so it is the only place in this library where a generic lambda
(`[](const auto& row)`) is allowed.

## Grouping — groupBy

```cpp
const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();

for (const auto& [item, period, rows] : producedByItemPeriod) { ... }     // every group (existing keys only)
const auto producingRows = producedByItemPeriod.slice(i, t);            // a specific key (empty group if absent)

for (const auto& [item, period, rows] : sPRODUCE.groupBy<ItemCol, PeriodCol>()) { ... }  // no name needed if you only iterate
```

- A group (`RowSlice`) is a random-access view onto the original set's rows. `producingRows.size()`,
  `producingRows.empty()`, `producingRows[k]`, `producingRows.front()`, `producingRows.back()`, range-for, and
  ranges algorithms all work. Rows are not copied.
- Iterating a `Grouped` unpacks, for each group, the key's columns followed by that group's rows
  (`[item, period, rows]`). If you need the key tuple as a whole, bind a single name and use `.key` and `.tuples`.
- If you only iterate, the `groupBy` result needs no name: range-for keeps the result of the range expression alive
  until the loop ends. Accesses that borrow a group, such as `slice()`, `at()`, and `keyAt()`, work only on a named
  `Grouped` (on a temporary they are a compile error).
- Groups come in order of each key's first occurrence; rows within a group keep the original set's order.
- **Iterating a `Grouped` visits existing keys only.** If the driver set (`sSTOCK`) has keys that this set
  (`sPRODUCE`) does not (an item nobody makes in that period), as in the quick start's inventory balance, write that
  constraint family **by iterating the driver set and calling `slice()`**, not by iterating the `Grouped`. That way
  empty keys get their constraint too.

## Variable families — VarTable

```cpp
const dreamor::concert::VarTable<KeyCols...> vName{env, set, lb, ub, type, "Name"};
```

- Built **all at once, from a set**. After construction its structure (which keys exist, their order, the mapping
  from key to variable) never changes. There is no `add` or `create`.
- `lb` and `ub` are each **a number or a column function**. Functions follow the same rule as `where` predicates.
  ```cpp
  const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
  ```
- Defaults when omitted: `lb = 0`, `ub = IloInfinity`, `ILOFLOAT`, no name.
- Semi-continuous variables: `dreamor::concert::VarTable<...> vBatch{dreamor::concert::semiContinuous, env, sBATCH, sclb, ub, ILOFLOAT, "Batch"};`
- Names are `name_key0_key1...` (`Yield_M0_I3_2`). Key tokens come from the `keyName()` hook (or `primaryKey()`).
  `nullptr` means no name.
- A VarTable takes its set in its constructor, so when you bundle a model into a struct, put the sets in a base class
  and the VarTables as members of a derived class (the base class is constructed first). The example's `Sets` /
  `Model` has that shape.

| Use | |
|---|---|
| `vYield(m, i, t)` / `vYield(row)` | The variable. `std::out_of_range` for a key not in the set |
| `vYield.contains(...)` | Whether the key exists |
| `vYield.size()` | Number of variables |
| `vYield.keys()` | The table's set |
| `for (const auto& [row, yieldVar] : vYield)` | (row, variable) in set order — the column order given to the model |
| `vYield.solution(cplex)` | Read the solution (below) |

**Tightening bounds later** needs no new API: pick rows with a set and change them with the solver API (below is
Concert; for MathOpt, `model.set_upper_bound(vYield(row), 0)`).

```cpp
const auto in_maintenance = [](MachineCol m, PeriodCol t) { return m->name == "M0" && t.get() == 2; };
for (const auto& row : vYield.keys().where(in_maintenance)) vYield(row).setUB(0);
```

Doing this before extraction (`IloCplex cplex(model)`) avoids change-notification costs.

**One table per variable family.** Reach the variables of a subset or a group with `vYield(row)`. Building another
table over a subset (`VarTable{env, sPRODUCE.where(p), ...}`) creates **a separate variable** for each of those rows.
The library cannot tell this apart from "different variable families over the same set" (the example's `vYield`
and `vSetup`), so it does not block it with a check.

A VarTable cannot be copied, only moved (a copy would have two names pointing at the same variables).

## Sums — sumOf

```cpp
dreamor::concert::sumOf(env, rows, term).build()                       // IloExpr
dreamor::concert::sumOf(env, rows, term).where(p, q).build()           // only rows where p AND q
dreamor::concert::sumOf(env, rows, term).split(p1, p2).build()         // std::array<IloExpr, 2>, several sums in one pass
dreamor::concert::sumOf<MachineCol>(env, machines, term).build()       // a plain container as a one-column set
```

- `rows` is a set or a group (`RowSlice`).
- `build()` is kept on purpose. Until `build()`, `sumOf(...)` is a builder, so `where()` and `split()` can be chained;
  using it without `build()` in a comparison, an assignment, `+=`, or an objective stops compilation with one
  sentence telling you to add `build()`.
- A term takes only the columns it reads and returns that row's term. Coefficients are computed from the entities
  (no separate coefficient arrays).
  ```cpp
  const auto setup_cost = [&](MachineCol m, ItemCol i, PeriodCol t) { return m->setup * vSetup(m, i, t); };
  ```
- To add up variables without coefficients, **pass the table itself as the term**. No lookup-only lambda is needed.
  ```cpp
  dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build()   // sum {m: (m,i,t) in PRODUCE} Yield[m,i,t]
  ```
  The table picks its key columns out of each row, so the rows may have more columns than the table.
- `split` computes the term only once per row.
- AMPL: `sum {(m,i,t) in PRODUCE} setup[m] * Setup[m,i,t]` → `sumOf(env, sPRODUCE, setup_cost)`

In Concert, the lifetime of the `IloExpr` returned by `build()` is the receiver's responsibility (see
[Concert resources and end()](#concert-resources-and-end) below). MathOpt's `LinearExpression` is an ordinary value,
so there is nothing to manage.

## Constraints

Constraints use **the solver API as is**. Below is Concert; for MathOpt it is `model.AddLinearConstraint(...)`. The
library has no constraint tables (for why, see [What dreamor does not do](#what-dreamor-does-not-do)).

```cpp
// A constraint family over a set: one per group
for (const auto& [machine, period, rows] : producedByMachinePeriod)
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);

// A single global constraint: setName if it needs a name
IloRange setupBudget = (dreamor::concert::sumOf(env, sPRODUCE, setup_cost).build() <= B);
setupBudget.setName("SetupBudget");
model.add(setupBudget);
```

- Concert has no `IloExpr(number)` constructor. Compare numbers directly (`expr <= 1`,
  `totalInFlow - currentDemand == endingStock`), and use variables as they are, without wrapping
  (`yieldVar <= ub * vSetup(row)`).
- If one key has two constraints (lower and upper), add both inside the loop.
- If only some keys have a constraint, filter the driver set with `where` first.
- Add names with `setName` when you need them. They help when reading LP exports or conflict analysis (finding the
  cause of infeasibility).

## Reading solutions

```cpp
const auto yieldPlan = vYield.solution(cplex);   // the whole table with one getValues
yieldPlan(m, i, t);                          // a value by key
for (const auto& [row, qty] : yieldPlan) { ... } // in set order
```

- The result (`ValueTable`) holds only doubles, so it stays usable after `cplex` or `env` has ended. It is the same
  type regardless of backend.
- Looking up a key not in the set (`yieldPlan(row)`) throws `std::out_of_range` instead of returning 0 (0 would be
  indistinguishable from "a variable whose value is 0").
- Concert's `solution()` accepts any algorithm that has `getEnv()` and `getValues(IloNumArray, IloNumVarArray)`. The
  temporary arrays it creates internally are cleaned up by itself.
- MathOpt's `solution()` takes the `SolveResult` returned by `math_opt::Solve` (`vYield.solution(*result)`). It
  throws `std::logic_error` if there is no feasible solution, because MathOpt itself would stop the process in that
  case (a CHECK failure).

## Concert resources and end()

Concert objects are not RAII. This library's policy is as follows.

- **One `env.end()` at the end.** It cleans up every Concert object. Do not call it twice.
- **A VarTable does not touch Concert when destroyed.** Variables are objects the model uses, and `env.end()`
  usually happens before the VarTable is destroyed.
- **Do not `end()` variables.** A variable's `end()` removes it from the model and from every constraint and deletes
  it. Calling `endElements()` on an array holding variables taken from a VarTable does the same.
  (`end()` on the array itself deletes only the array and leaves the variables.)
- **A temporary `IloExpr` is the creator's (receiver's) responsibility.** After building the constraint, it is safe
  to `end()` the expression (the expression is deep-copied into the constraint). In models with millions of
  constraints, the memory difference is noticeable.

This section does not apply to MathOpt. `math_opt::Model` and `LinearExpression` are ordinary C++ objects that are
cleaned up when they go out of scope, and there is no call like `end()`. A variable (`math_opt::Variable`) is a value
that points at its model, so use it only while the model is alive.

## Rules

1. Callables (predicates, dependent sources, bound functions, terms of sums) take **column types** as parameters.
   `[](const Item* i)` or `[](auto i)` is a compile error. The only exception is the key of `orderBy` (which takes
   the whole row).
2. A dependent source returns a range of **pointers** to entities.
3. Sets are read-only. Derive instead of modifying.
4. One VarTable per variable family, over the family's **whole set**. Reach parts with `vYield(row)`.
5. If every key of the driver set needs a constraint, do not iterate the `Grouped`; iterate the driver and call
   `slice()`.
6. No source holds an element twice, and an entity is one object in one container. Sets then hold no row twice.
7. Do not `end()` variables. `env.end()` once, at the end.
8. Use `Grouped`'s `slice()`/`at()`/`keyAt()` and `Ordered`'s lookups on named objects. A group is valid only while
   that object is alive. If you only iterate, a temporary works too, as in `for (... : S.groupBy<C>())`.
9. A function object whose `operator()` is a template, so that its columns cannot be read from its parameters, can
   be used as a predicate by specializing `dreamor::PredicateColumns` to declare the columns it reads.
   ```cpp
   template <> struct dreamor::PredicateColumns<MyFilter> { using type = std::tuple<MachineCol, PeriodCol>; };
   ```
10. In generic code (`[](const auto& sANY)`), calling a member that takes columns as template arguments, such as
    `sANY.select<A>()`, requires `sANY.template select<A>()`. This does not come up in the recommended style, which
    handles sets by their concrete types.
11. Look up by key with column values in declaration order (`vYield(m, i, t)`, `slice(i, t)`, `prev(t)`). Braces and
    bare values are not accepted (see [Columns](#columns)).

## What dreamor does not do

| Not done | Reason / alternative |
|---|---|
| Constraint tables (storing constraints per key, key-based constraint names) | Most constraints are added once and never looked up again. If needed, use the solver API (Concert `IloRange` + `setName`, MathOpt `AddLinearConstraint(expr, name)`) |
| Reading duals / reduced costs by key | A rare task. Use the solver API when needed (Concert `getDuals`, MathOpt `dual_values()`, etc.) |
| Appending variables (column generation) | Structure is immutable. New columns mean a new set + a new VarTable |
| Pipe syntax (`S \| where(p)`) | No second way to do the same thing |
| Coefficient arrays (`IloScalProd` style) | Coefficients are computed from entities in the term |
| Checking for duplicate variable creation | Covered by rule 4 instead |
| Constraint programming (interval variables, `noOverlap`, cumulative functions) | Outside the scope of an algebraic modeling layer. The set layer is solver-independent, so when writing scheduling with CP Optimizer or CP-SAT you can still pass rows built with `makeSet`, `groupBy`, and `ordered()` straight to that API |

## Public API reference

**Columns** (`column.hpp`)
| | |
|---|---|
| `Col<T, "name">` | Column type. `Col(T)` (implicit), `operator T()`, `get()`, `->` and `*` for pointers, `==`, default construction |
| `Col<...>::kName` | Column name (`std::string_view`) |
| `ColumnName` | Type of `Col`'s name template argument (a string literal) |
| `keyName(Col)` | The column's name token. The value's `keyName` if it has one, else an entity's `primaryKey()`, otherwise the value itself |
| `std::hash<Col<...>>` | Lets a column be a key of `std::unordered_map` and the like |
| `PredicateColumns<F>` | Specialization point declaring the columns read by a function object with a template `operator()` |

**Building sets** (`set.hpp`)
| | |
|---|---|
| `makeSet<Cols...>(sources...)` | Set builder |
| Builder `.where(p)` | Adds one predicate (call repeatedly for several) |
| Builder `.collect()` | Finalizes into an `IndexSet` |

**Sets** (`indexed.hpp`, implemented in `set.hpp`/`algebra.hpp`/`grouped.hpp`/`ordered.hpp`)
| | |
|---|---|
| `IndexSet<std::tuple<Cols...>>`, alias `SetOf<Cols...>` | Read-only set. Default construction (empty set), O(1) copy |
| `begin()`, `end()`, `size()` | Random-access iteration |
| `where(p...)`, `orderBy(key)`, `select<Cols...>()`, `select(expr)` | Derivation → `IndexSet` |
| `groupBy<Cols...>()` | → `Grouped` |
| `membership()` | → `Membership` |
| `ordered()` | → `Ordered` |
| `unite(A, B)`, `intersect(A, B)`, `difference(A, B)` | Binary operations → `IndexSet` |

**EntityVector** (`entity_vector.hpp`)
| | |
|---|---|
| `primaryKey(const T&)` | ADL hook next to `T`: what identifies it (a value or a `std::tuple`). `dreamor::Entity<T>` checks for it |
| `insert(entity)`, `emplace(args...)` | `{iterator to the element with that key, whether it went in}` |
| `find(key)`, `contains(key)` | By primary key |
| `operator[]`, `at`, `front`, `back`, `begin`, `end`, `size`, `empty` | Const access, insertion order |
| `modify(entity, fn)` | `fn(T&)` on one element; its key must not change |

**Membership** (`algebra.hpp`)
| | |
|---|---|
| `(cols...)` / `(row)` | Membership as `bool` |

**Grouped** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()` | Every existing group (in first-occurrence order). The element `Group` unpacks as `[cols..., rows]` |
| `Key`, `Group{key, tuples}` | Group key tuple, one group |
| `slice(cols...)`, `slice(key)` | That key's `RowSlice` (empty if absent). `key` comes from `keyAt()` or `Group::key` |
| `contains(cols...)`, `sliceSize(cols...)` | Key existence, group size (also take `key`) |
| `size()`, `empty()` | Number of groups |
| `keyAt(g)`, `at(g)` | Key of the g-th group, the group (`std::out_of_range` if out of range) |
| `regrouped()` | A new `IndexSet` with rows gathered group by group |

**RowSlice** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()`, `size()`, `empty()`, `operator[](i)`, `front()`, `back()` | Random-access view, default construction (empty) |

**Ordered** (`ordered.hpp`)
| | |
|---|---|
| `ord(cols...)` | Position (`std::optional<std::size_t>`, empty if absent). Everything below also takes a single row instead of columns |
| `next(cols...)`, `next(cols..., steps)`, `prev(...)` likewise | Pointer to the neighboring row, or `nullptr` |
| `nextCyclic(...)`, `prevCyclic(...)` | Neighbors that wrap from the end back to the start |
| `first()`, `last()`, `at(pos)` | First, last, by position |
| `isFirst(cols...)`, `isLast(cols...)`, `contains(cols...)` | Tests |
| `size()`, `empty()`, `members()` | Size, the deduplicated set |

**Variables** (`concert.hpp`, namespace `dreamor::concert`)
| | |
|---|---|
| `VarTable<Keys...>{env, set, lb = 0, ub = IloInfinity, type = ILOFLOAT, name = nullptr}` | lb, ub are numbers or column functions |
| `VarTable<Keys...>{semiContinuous, env, set, sclb, ub, type, name}` | Semi-continuous |
| `Key` | `std::tuple<Keys...>` |
| `(keys...)`, `(row)` | The variable: `vYield(m, i, t)`, `vYield(row)` (`std::out_of_range` if absent, common to all backends) |
| `contains(keys...)`, `contains(row)`, `size()`, `keys()` | |
| `begin()`, `end()` | `(row, var)` iteration |
| `solution(algorithm)` | → `ValueTable` |
| `SemiContinuous`, `semiContinuous` | Tag for the semi-continuous constructor |

**Values** (`value_table.hpp`, namespace `dreamor`, common to all backends)
| | |
|---|---|
| `ValueTable<Keys...>` | Values by `(keys...)`/`(row)`: `yieldPlan(m, i, t)` (`std::out_of_range` if absent), `contains`, `size()`, `keys()`, `(row, value)` iteration |

**Sums** (`concert.hpp`, namespace `dreamor::concert`)
| | |
|---|---|
| `sumOf(env, rows, term)`, `sumOf<Col>(env, container, term)` | Sum builder. The term is a column function or a `VarTable` itself |
| Builder `.where(p...)`, `.split(p...)`, `.build()` | Filter, split sums, `IloExpr` or `std::array<IloExpr, N>` |

**MathOpt backend** (`mathopt.hpp`, namespace `dreamor::mathopt`)
| | |
|---|---|
| `VarTable<Keys...>{model, set, lb = 0, ub = +inf, is_integer = false, name = nullptr}` | `model` is `math_opt::Model&`; lb, ub are numbers or column functions |
| `(keys...)`, `(row)`, `contains`, `size`, `keys`, `begin`/`end` | Same as Concert (variables are `math_opt::Variable`) |
| `solution(result)` | `math_opt::SolveResult` → `ValueTable`; `std::logic_error` if there is no feasible solution |
| `sumOf(rows, term)`, `sumOf<Col>(container, term)` | Sum builder; `build()` gives `math_opt::LinearExpression` (an array of them with `split`) |

## Tests and development environment

The tests run without a framework, through a single `tests/run_tests.sh`. Choose the compiler with `CXX` (default
`g++`, C++20).

**Compilers:** GCC 13 or later, Clang 18 or later. CI tests both ends of that range - GCC 13 and 16, Clang 18 and 23 -
and nothing in between; MSVC and Apple Clang are not tested. To try the newest ones locally: Clang from
[apt.llvm.org](https://apt.llvm.org/) (`CXX=clang++-23 tests/run_tests.sh`), GCC from its official container image
(`podman run --rm -v "$PWD":/src -w /src docker.io/library/gcc:16 sh -c 'CXX=g++ tests/run_tests.sh'`; `docker` takes
the same arguments).

| Command | What | Requires |
|---|---|---|
| `tests/run_tests.sh` | The whole set layer and Concert backend (on a Concert stub), that code which must not compile is rejected, and the syntax of the CPLEX-only code | Nothing |
| `tests/run_tests.sh --cplex [DIR]` | Verification by solving with real CPLEX, running the example | CPLEX Studio (`$CPLEX_STUDIO_DIR`, default `/opt/ibm/ILOG/CPLEX_Studio2212`) |
| `tests/run_tests.sh --mathopt [DIR]` | Verification of the MathOpt backend by solving with HiGHS, running the example, building the same example with both backends and comparing the constraints, running the ported OR-Tools samples (SCIP) | OR-Tools C++ prebuilt (`$ORTOOLS_DIR`, default `/opt/ortools`) |

Code that must not compile lives one case per file in `tests/compile_fail/` (Concert) and
`tests/mathopt/compile_fail/` (MathOpt). The runner does not only check that each case is rejected: a
`// expect: <text>` line at the top of the file must appear in the compiler's **first error** (and its notes). The
point is that what a user reads before the next error is the intended one-sentence message. Cases without
`// expect:` only have to be rejected. `control.cc` in the same folder is a control case checking that the shared
prelude compiles; if it fails, the other cases would be meaningless, so it counts as a failure.

CI (GitHub Actions) runs the stub and MathOpt modes with both GCC 13 and Clang 18 (checking the `// expect:` text
with both compilers), the stub mode again at `-O3 -DNDEBUG`, under AddressSanitizer and UndefinedBehaviorSanitizer,
and with GCC 16 and Clang 23, and goes as far as installing the CMake package and consuming it via `find_package`
(`tests/cmake_consumer`, `tests/cmake_consumer_mathopt`). CPLEX is not on CI, so run `--cplex` locally.

**Compile cost of wide sets.** One translation unit that builds a set of N columns, filters it, groups it, puts a
`VarTable` on it and sums over it (`-std=c++20 -O2 -c` without `NDEBUG`, so with the debug build's contract checks,
which `-DNDEBUG` compiles out; CPU time and peak memory; AMD Ryzen 7 5700X, WSL2 Ubuntu 24.04, 2026-10-24). `tests/test_arity.cpp` runs 63 and 64 columns on every test run, so a blow-up would show there;
`python3 tools/measure_compile_cost.py [compiler ...]` re-measures the table.

| Columns | g++ 13.3 | clang++ 18.1 | clang++ 23.1 |
|---|---|---|---|
| 4 | 3.1 s, 309 MB | 3.0 s, 216 MB | 3.0 s, 230 MB |
| 16 | 3.7 s, 331 MB | 3.7 s, 244 MB | 4.1 s, 257 MB |
| 32 | 4.6 s, 384 MB | 5.1 s, 297 MB | 4.9 s, 310 MB |
| 63 | 6.7 s, 533 MB | 8.9 s, 466 MB | 8.6 s, 472 MB |
| 64 | 6.9 s, 540 MB | 8.9 s, 474 MB | 8.4 s, 479 MB |

OR-Tools needs no installation: unpack the release's C++ prebuilt anywhere and point to that folder (no CMake needed).

```bash
mkdir -p ~/opt && cd ~/opt
curl -L https://github.com/google/or-tools/releases/download/v9.15/or-tools_amd64_ubuntu-24.04_cpp_v9.15.6755.tar.gz | tar xz
export ORTOOLS_DIR=~/opt/or-tools_x86_64_Ubuntu-24.04_cpp_v9.15.6755
```

If you are not on Ubuntu 24.04, pick the C++ archive for your distribution from the releases page.

The editor setup assumes clangd. `python3 tools/gen_compile_commands.py` generates `compile_commands.json` (CPLEX and
OR-Tools paths come from the environment variables above), and the repository's `.clangd` and
`.vscode/settings.json` point at it. `compile_commands.json` differs per machine, so it is not committed.

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
