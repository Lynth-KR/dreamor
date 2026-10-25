---
name: dreamor
description: Write, review, and fix C++ optimization models that use dreamor (index sets, VarTable, sumOf) on CPLEX Concert or OR-Tools MathOpt. Use this skill whenever code includes "dreamor/..." headers or uses makeSet, SetOf, groupBy, slice, ordered, VarTable, ValueTable, or sumOf; whenever the user wants to translate an AMPL-style model (sets, indexed variables, sums, constraint families) into C++ with dreamor; and whenever a compile error mentions dreamor, VarTable, sumOf, UnbuiltSum, or messages such as "Look a variable or value up by the table's key columns" or "is not an expression yet".
---

# dreamor (0.2)

dreamor is a header-only C++20 layer for algebraic optimization models. It provides exactly three things:

1. **Index sets**, built like AMPL set declarations. These are solver-independent and come from
   `dreamor/index_set.hpp`.
2. **Variable families** (`VarTable`), one table of solver variables per set.
3. **Sums** (`sumOf`) over a set or a group.

Variable families and sums are backend-specific. `dreamor::ValueTable` (solution values) is shared by both backends.

Everything else — constraints, objectives, solving, duals, parameters — is written in the solver's own API.
dreamor has no constraint tables, no column generation (tables never grow), and no constraint programming
(interval variables, `noOverlap`, cumulative functions). Do not invent API for these.

Read further only when needed:
- `references/api.md` — full signatures: sets, `Grouped`, `RowSlice`, `Ordered`, both backends, `ValueTable`.
- `references/diagnostics.md` — every dreamor compile-time message with its cause and fix, plus runtime
  exceptions. Read it whenever you are fixing a build error in dreamor code.

## Headers and namespaces

| Include | Namespace | Use with |
|---|---|---|
| `dreamor/index_set.hpp` | `dreamor` | any solver |
| `dreamor/concert.hpp` | `dreamor::concert` | CPLEX Concert (`IloEnv`, `IloModel`, `IloCplex`, `ILOFLOAT`/`ILOINT`/`ILOBOOL`) |
| `dreamor/mathopt.hpp` | `dreamor::mathopt` | OR-Tools MathOpt (`math_opt::Model`, `math_opt::Solve`) |

There is no `dreamor::VarTable` and no `dreamor::sumOf`. Qualify them with the backend namespace, or alias it:
`namespace concert = dreamor::concert;`.

CMake: `find_package(dreamor 0.2 CONFIG REQUIRED)`, then link `dreamor::dreamor` plus the solver
(`ortools::ortools`, or the user's CPLEX target).

## Naming

Three conventions are prescribed. Use them in generated code:

| Kind | Shape | Example |
|---|---|---|
| Column type | PascalCase + `Col` | `MachineCol`, `PeriodCol` |
| Set | `s` + SCREAMING_SNAKE_CASE | `sPRODUCE` (AMPL `PRODUCE`) |
| Variable family | `v` + PascalCase | `vYield` (AMPL `Yield`) |

Everything else is the user's choice. Unless the user's code shows another style, write callables that take columns
in snake_case (`items_of`, `machine_usage`) and everything else in camelCase (`producedByMachinePeriod`,
`yieldPlan`). Prefer self-describing names over single letters.

## Rules

These are enforced by the compiler or needed for a correct model.

1. **Callables take column types.** Predicates, dependent sources, bounds, and sum terms are written as
   `[](MachineCol m, PeriodCol t) { ... }`, taking only the columns they read, in any order. dreamor reads the
   parameter types to know which columns to bind, so `[](const Machine* m)`, `[](int t)`, and `[](auto x)` are
   rejected. The one exception is the key of `orderBy`, which takes the whole row (`[](const auto& row)`).
2. **Lookups take column values in declaration order, or one row.** This covers `vYield(m, i, t)`,
   `yieldPlan(m, i, t)`, `grouped.slice(i, t)`, `orderedPeriods.prev(t)`, `orderedPeriods.next(t, 2)`, and
   `in_routes(m, i)`. Bare values (`vYield(2, 1, 0)`) and braces (`slice({i, t})`) are rejected, because two
   int-valued columns could otherwise swap places silently. Wrap a raw value: `PeriodCol{2}`.
3. **`sumOf(...)` becomes an expression only after `.build()`.**
   - Concert: `dreamor::concert::sumOf(env, rows, term).build()` → `IloExpr`.
   - MathOpt: `dreamor::mathopt::sumOf(rows, term).build()` → `LinearExpression`. It takes no env and no model.

   `build()` is deliberate, since it lets `.where()`/`.split()` chain; keep it.
4. **To sum variables without coefficients, pass the table itself as the term.** Write
   `sumOf(env, rows, vYield)` with no lambda.
5. **One `VarTable` per variable family, over the family's whole set.** A second table over a subset creates
   new, unrelated variables. Reach subsets and groups through the one table: `vYield(row)`. Two tables over one set
   are fine when they are different families (`vYield` and `vSetup` over `sPRODUCE`).
6. **A table's key columns equal its set's columns, in order.** Reorder a set with `select<...>()` if needed.
7. **Sets are read-only.** To get a different set, derive one with `where`, `select`, `orderBy`, `unite`,
   `intersect`, or `difference`.
8. **Name the object before borrowing from it.** Call `Grouped::slice/at/keyAt` and every `Ordered` lookup on a
   named variable. Plain iteration of a temporary is fine: `for (const auto& [c, rows] : S.groupBy<C>())`.
9. **Grouped iteration binds key columns, then rows:** `for (const auto& [machine, period, rows] : grouped)`.
10. **Iterating a `Grouped` visits only keys present in that set.** If every key of a driver set needs a
    constraint (including keys with no rows), iterate the driver set and call `slice()`. A missing key gives an
    empty slice, whose sum is 0.
11. **Sources.** A dependent source returns a range of *pointers* (`std::vector<const Item*>`, a span, a view). A
    container source must be an lvalue. No source holds an element twice, and an entity is one object in one
    container: a precondition (undefined behavior if broken) that keeps every set free of repeated rows.
    `dreamor::EntityVector<T>` keeps it for entities: define `primaryKey(const T&)` next to `T`, then
    `insert`/`emplace` return `{element, inserted}` as `std::set` does, and `modify(entity, fn)` is the only way to
    change one.
12. **Concert lifetime.** Call `env.end()` exactly once, last. Never `end()` a VarTable's variables or
    `endElements()` an array holding them. `end()` temporary `IloExpr`s after adding the constraint. None of this
    applies to MathOpt.
13. **Read columns by name.** Write `std::get<PeriodCol>(row)`, not `std::get<0>(row)`.

## AMPL → dreamor

| AMPL | dreamor |
|---|---|
| `set ROUTES := {m in MACHINES, i in ITEMS[m]};` | `const auto sROUTES = dreamor::makeSet<MachineCol, ItemCol>(machines, items_of).collect();` |
| `set PRODUCE := {(m,i) in ROUTES, t in PERIODS};` | `const auto sPRODUCE = dreamor::makeSet<PeriodCol>(sROUTES, periods).collect();` |
| `{...: open[i,t]}` | `.where(is_open)` with `[](ItemCol i, PeriodCol t) { ... }` |
| `setof {(m,i,t) in S} (m,i)` | `S.select<MachineCol, ItemCol>()` |
| `{(m,i) in ROUTES, (i,t) in OPEN}` | `dreamor::makeSet<>(sROUTES, sOPEN).collect()` (joined on the shared `ItemCol`) |
| `setof {(i,j) in EDGES} (j,i)` | `sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; })` |
| `(m,i) in ROUTES` | `sROUTES.membership()`, called as `in_routes(m, i)` or passed to `where` |
| `prev(t)`, `next(t, 2)`, `ord(t)` on an ordered set | `const auto orderedPeriods = sPERIODS.ordered();` then `.prev(t)` (row pointer or `nullptr`), `.next(t, 2)`, `.ord(t)` (0-based optional) |
| `var Yield {PRODUCE} >= 0, <= cap[m]/time[i];` | `const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};` |
| `sum {(m,i,t) in PRODUCE} setup[m] * Setup[m,i,t]` | `dreamor::concert::sumOf(env, sPRODUCE, setup_cost).build()` |
| `sum {m: (m,i,t) in PRODUCE} Yield[m,i,t]` | `dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build()` |
| `subject to C {m in MACHINES, t in PERIODS}: ...` | `for (const auto& [machine, period, rows] : sPRODUCE.groupBy<MachineCol, PeriodCol>()) model.add(...);` |

## Canonical model (Concert)

```cpp
#include <ilcplex/ilocplex.h>
#include "dreamor/index_set.hpp"
#include "dreamor/concert.hpp"

struct Item { std::string name; double time, hold; std::vector<double> demand; };
struct Machine { std::string name; double capacity, setup; std::vector<const Item*> items; };
inline const std::string& keyName(const Item* i) { return i->name; }      // token in variable names,
inline const std::string& keyName(const Machine* m) { return m->name; }   // same namespace as the entity

using MachineCol = dreamor::Col<const Machine*, "machine">;
using ItemCol    = dreamor::Col<const Item*, "item">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;

IloEnv env;
IloModel model(env);

const auto items_of = [](MachineCol m) { return m->items; };
const auto sPERIODS = dreamor::makeSet<PeriodCol>(periods).collect();
const auto sROUTES  = dreamor::makeSet<MachineCol, ItemCol>(machines, items_of).collect();
const auto sPRODUCE = dreamor::makeSet<PeriodCol>(sROUTES, periods).collect();
const auto sSTOCK   = dreamor::makeSet<ItemCol, PeriodCol>(items, periods).collect();

const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"};
const dreamor::concert::VarTable<ItemCol, PeriodCol> vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"};

// Every (item, period) needs a balance row, even with no producer: iterate the driver set + slice().
const auto orderedPeriods = sPERIODS.ordered();
const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
for (const auto& [i, t] : sSTOCK) {
  IloExpr totalInFlow = dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build();
  if (const auto* prevPeriod = orderedPeriods.prev(t)) totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
  model.add(totalInFlow - i->demand[t.get()] == vStock(i, t));
  totalInFlow.end();
}

// One row per existing (machine, period): iterate the Grouped.
const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
for (const auto& [machine, period, rows] : sPRODUCE.groupBy<MachineCol, PeriodCol>()) {
  IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
  model.add(totalLoad <= machine->capacity);
  totalLoad.end();
}

for (const auto& [row, yieldVar] : vYield) model.add(yieldVar <= yieldVar.getUB() * vSetup(row));

IloExpr holdingCost = dreamor::concert::sumOf(env, sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
IloExpr setupCost = dreamor::concert::sumOf(env, sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) { return m->setup * vSetup(m, i, t); }).build();
IloExpr totalCost = holdingCost + setupCost;
model.add(IloMinimize(env, totalCost));
holdingCost.end();
setupCost.end();
totalCost.end();

IloCplex cplex(model);
cplex.solve();
const auto yieldPlan = vYield.solution(cplex);   // dreamor::ValueTable, usable after env.end()
for (const auto& [row, qty] : yieldPlan) { /* ... */ }
env.end();
```

## The same model on MathOpt

Only the solver-facing parts change, and they take MathOpt's own notation:

```cpp
namespace math_opt = ::operations_research::math_opt;
math_opt::Model model("lot sizing");
const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vYield{model, sPRODUCE, 0, max_possible_qty, /*is_integer=*/false, "Yield"};
const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{model, sPRODUCE, 0, 1, true, "Setup"};

for (const auto& [machine, period, rows] : sPRODUCE.groupBy<MachineCol, PeriodCol>()) {
  const math_opt::LinearExpression totalLoad = dreamor::mathopt::sumOf(rows, machine_usage).build();
  model.AddLinearConstraint(totalLoad <= machine->capacity);
}

const auto result = math_opt::Solve(model, math_opt::SolverType::kHighs);
const auto yieldPlan = vYield.solution(*result);   // std::logic_error if no primal feasible solution
```

- The table takes a `Model&` instead of an env, and `is_integer` instead of a type constant.
- `sumOf` takes no env.
- There are no semi-continuous variables, no `end()` calls, and sums are linear: a term returning a
  `QuadraticExpression` does not compile.
- Bounds are tightened with `model.set_upper_bound(vYield(row), 0)`.

## Patterns

- **Tighten bounds after construction:** `for (const auto& row : vYield.keys().where(in_maintenance)) vYield(row).setUB(0);`
  In Concert, do this before `IloCplex cplex(model)`.
- **Constraint for only some keys:** filter the driver set with `where` before the loop.
- **Lower and upper constraint per key:** add both in the same iteration.
- **One named global constraint (Concert):** `IloRange r = (expr <= B); r.setName("SetupBudget"); model.add(r);`
- **Duals and reduced costs:** use the solver API (`getDuals`, `dual_values()`). There is no keyed accessor.
- **Several sums in one pass:** `.split(p1, p2).build()` returns `std::array<..., 2>` and computes each term once.
- **Model in a struct:** put the sets in a base class and the `VarTable`s as members of a derived class (the base is
  constructed first).
- **Generic code over sets:** `sANY.template select<A>()`.
- **A function object with a template `operator()` as a predicate:** specialize
  `dreamor::PredicateColumns<F>` with `using type = std::tuple<Cols...>;`.
- **Scheduling with CP Optimizer or CP-SAT:** dreamor does not model it, but rows from `makeSet`/`groupBy`/`ordered()`
  can be passed to those APIs directly.

## Before finishing

Check generated code against this list:
- Every callable takes column types.
- Every lookup passes columns in key order.
- Every `sumOf` ends in `.build()` and uses the right backend's argument list.
- No `VarTable` is built over a subset of another family's set.
- Driver-set constraint families use `slice()` rather than `Grouped` iteration.
- In Concert, `env.end()` appears once.
