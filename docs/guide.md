# Guide

**English** · [한국어](ko/guide.md)

Every part of dreamor in the order a model uses them: columns and entities, building sets and deriving them,
grouping, variable families, sums, constraints and solutions. The [README's quick start](../README.md#quick-start)
shows them together in one model; the [API reference](reference.md) lists every member in tables; the
[philosophy](philosophy.md) gives the principles and the rules of use.

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
[public API reference](reference.md#public-api-reference).

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
library has no constraint tables (for why, see [What dreamor does not do](philosophy.md#what-dreamor-does-not-do)).

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

## Compile cost of wide sets

One translation unit that builds a set of N columns, filters it, groups it, puts a
`VarTable` on it and sums over it (`-std=c++20 -O2 -c` without `NDEBUG`, so with the debug build's contract checks,
which `-DNDEBUG` compiles out; CPU time and peak memory; AMD Ryzen 7 5700X, WSL2 Ubuntu 24.04, 2026-10-25). `tests/test_arity.cpp` runs 63 and 64 columns on every test run, so a blow-up would show there;
`python3 tools/measure_compile_cost.py [compiler ...]` re-measures the table.

| Columns | g++ 13.3 | clang++ 18.1 | clang++ 23.1 |
|---|---|---|---|
| 4 | 3.3 s, 315 MB | 3.0 s, 216 MB | 3.1 s, 230 MB |
| 16 | 3.8 s, 331 MB | 3.6 s, 246 MB | 3.7 s, 258 MB |
| 32 | 4.6 s, 390 MB | 4.9 s, 301 MB | 4.7 s, 312 MB |
| 63 | 7.6 s, 569 MB | 8.8 s, 469 MB | 8.3 s, 474 MB |
| 64 | 7.8 s, 580 MB | 9.0 s, 477 MB | 8.5 s, 482 MB |
