# Guide

**English** · [한국어](ko/guide.md)

Every part of dreamor in the order a model uses them: columns and entities, building sets and deriving them,
grouping, variable families, sums, constraints and solutions. The [README's quick start](../README.md#quick-start)
shows them together in one model; the [API reference](reference.md) lists every member in tables; the
[philosophy](philosophy.md) gives the principles and the rules of use.

## Columns

```cpp
using cMachine = dreamor::Col<const Machine*, "machine">;   // value type + name
using cPeriod  = dreamor::Col<std::size_t, "period">;       // the value may be an integer
```

- A `Col` converts implicitly to the value it wraps, and if that value is a pointer, `->` and `*` reach the entity
  directly (`m->capacity`). Get the value out with `t.get()`.
- One entity type may have more than one column. Different names make different columns (`cFrom` and `cTo`,
  both `const Node*`).
- **Tokens for variable names**: for an entity-pointer column, a `keyName(const T*)` function placed in the **same
  namespace** as the entity is used, or else the entity's `primaryKey()` (next section; a debug build says so once
  per column). Values printable with `std::format` (integers, strings) need no hook.
- **An entity column holds only the entity's address**, so it may be declared before the entity is complete, with
  the entity forward-declared: every column of a model at the top of a file, the entities that are computed from
  the data defined further down. Define the entity's `keyName` (or `primaryKey`) right under the entity, before
  anything names a column of it: the hook is looked up where a name is first made, and a name made earlier would be
  made without it.
- **Lookups by key take column values only.** `vYield(m, i, t)`, `yieldPlan(m, i, t)`, `slice(i, t)`, `prev(t)`, and
  `in_routes(m, i)` take the key's columns in declaration order, or one row of the set. Even though `Col` converts
  implicitly from its value, bare values (`x(2, 1)`, a bare pointer) and braces (`slice({i, t})`) are not accepted;
  compilation stops with one sentence telling you what to change. This keeps two int-valued columns from silently
  passing in swapped positions. If you start from a value, wrap it: `cPeriod{2}`.

## Entities — EntityVector

A set keyed by entity pointers counts on each entity being one object: two objects for one machine are two machines
to every set built on them. `dreamor::EntityVector<T>` holds each entity once, by the primary key the entity
declares next to its type, as it does `keyName`:

```cpp
inline const std::string& primaryKey(const Machine& m) { return m.name; }     // found by ADL

dreamor::EntityVector<Machine> plant;
const bool inserted = plant.emplace(Machine{"M1", 40.0, 5.0, {}}).second;      // false: M1 was there
plant.sort().freeze();                                                         // primary-key order; then read only
const auto sRUN = dreamor::makeSet<cMachine, cPeriod>(plant, periods).collect();
const auto lowerSetup = [](Machine& m) { m.setup = 4.0; };
if (const Machine* found = plant.find("M1")) {
  plant.modify(found, lowerSetup);                                            // the one way to change one
}
```

- It is filled, frozen, then read, the order a model's data goes in. Before `freeze()`: `reserve(n)`; `insert`
  and `emplace`, which return `{the element with that key, whether it went in}` as `std::set`'s do, the element
  lasting as a `std::vector`'s does - until a growth past the capacity reserved, or a sort; and `sort()`, in
  primary-key order, or `sort(comp)`, in comp's as `std::sort` takes it. After `freeze()`: indexing, iteration,
  `find(key)` (the element or null), `contains(key)` and `modify`. A debug build stops on either side crossed.
- Once frozen, elements never move and nothing is erased, so pointers into the model stay the entity's. Copying is
  deleted - a copy would be a second object for every entity; moving keeps each in place.
- Access is const. `modify(entity, fn)` changes one in place, and its primary key must come out the same (a debug
  build checks). Sets are snapshots: a set or a model already built is not recomputed, so `modify` is for writing
  results back afterwards - which usually belong in a `ValueTable`.
- A composite key is a `std::tuple`: `return std::tuple{r.plant, r.code};`.
- It is a choice, not a requirement: any container that keeps the contract below is a source.

## Building sets — makeSet

```cpp
const auto sNAME = dreamor::makeSet<Cols...>(sources...).where(predicate).where(predicate)...collect();
```

Each column of `makeSet<A, B, C>` corresponds to one source. There are four kinds of source.

| Source | Example | Meaning |
|---|---|---|
| Container | `machines` | Every value of that column. The elements may be pointers (`std::vector<const Machine*>`), the entities themselves (`std::vector<Machine>`), or smart pointers (`std::vector<std::unique_ptr<Machine>>`); any of them yields a pointer column |
| Interval | `dreamor::interval::closed(1, T)` | The integers from 1 to T; `open`, `closedOpen` and `openClosed` leave out an end or both. The values take the column's integer type |
| Dependent source | `[](cMachine m) { return m->items; }` | Values that depend on columns already fixed earlier |
| Existing set | `makeSet<cPeriod>(sROUTES, periods)` | Fills all of that set's columns at once. Two sets that share a column are joined on it |

- **AMPL correspondence**
  - `{m in MACHINES, i in ITEMS[m]}` → `makeSet<cMachine, cItem>(machines, items_of).collect()`
  - `{(m,i) in ROUTES, t in PERIODS: open[i,t]}` → `makeSet<cPeriod>(sROUTES, periods).where(is_open).collect()`
  - `{(m,i) in ROUTES, (i,t) in OPEN}` → `makeSet<>(sROUTES, sOPEN).collect()`
  - `{m in MACHINES, t in 1..T}` → `makeSet<cMachine, cPeriod>(machines, dreamor::interval::closed(1, T)).collect()`
- **Set sources that share a column are joined on it** (a natural join). The one visited second is looked up by the
  shared columns instead of scanned, so the product is never built: the cost is the two sets plus the result. A set
  whose columns all lie in another one filters it, with the same rows as `sROUTES.where(sOPEN.membership())`. A
  column named in `makeSet<...>` is always new; repeating a set source's column there is a compile error.
- **A set has at most 64 columns**, the columns of a set source included: while the visit order is planned, a
  set's columns are bits of one 64-bit mask. A 65th stops at compile time on dreamor's own message.
- **A predicate takes only the columns it reads.** `[](cItem i, cPeriod t)` is evaluated as soon as i and t are
  fixed, and if it fails, the whole loop below is skipped. Parameter order and `.where()` order are free.
- A dependent source must return a **range of pointers to entities** (`std::vector<const Item*>`, a span, a view),
  not the entities themselves.
- A container source must be an lvalue (no temporary vectors); an interval is handed over as it is.
- **No source holds an element twice**: a container, the range a dependent source returns, a set. An entity is one
  object, held by one container, so two objects for one machine are a repeat too (`EntityVector` keeps that for
  entities). This is a precondition, as in the standard library: breaking it is undefined behavior, and a debug build
  (no `NDEBUG`) stops on it with a message naming the source. In return, a set never holds a row twice - no
  operation below makes a repeat - and each key of a VarTable has exactly one variable.
- Rows come out in generation order, and which column varies fastest is not fixed. If order matters, sort with
  `orderBy`.
- To spell out a set's type, use `dreamor::SetOf<cMachine, cItem>`.

## Steering the visit order

`makeSet` picks the order it visits its sources in at compile time, from their columns alone: every predicate runs as
soon as the columns it reads are bound, and a source that lets predicates run sooner comes sooner. It knows no sizes,
so it cannot tell a predicate that keeps one row in fifty from one that keeps them all. When you know which part is
selective, build that part first, as a set of its own, and hand the set on:

```cpp
// {m in MACHINES, i in ITEMS, t in PERIODS: 30 * time[i] <= capacity[m] and demand[i,t] > 0}
const auto fits_batch = [](cMachine m, cItem i) { return 30 * i->time <= m->capacity; };
const auto sMAKE = dreamor::makeSet<cMachine, cItem, cPeriod>(machines, items, periods)
                       .where(fits_batch)
                       .where(is_open)
                       .collect();

// The same rows in two steps: is_open over items x periods alone, then fits_batch over what passed
const auto sOPEN        = dreamor::makeSet<cItem, cPeriod>(items, periods).where(is_open).collect();
const auto sMAKE_STAGED = dreamor::makeSet<cMachine>(machines, sOPEN).where(fits_batch).collect();
```

- **The same set.** The rows are the same, and so is the type: a set's columns come in the order of its sources, so
  `machines` first gives `(machine, item, period)`. Only the row order may differ (`orderBy` if it matters).
- **What the step fixes** is which predicate runs over which product, not the order of the loops: inside each
  `makeSet` the planner still chooses. Above, `is_open` runs once for every item and period instead of once for every
  machine, item and period whose batch fits - with few open pairs and most batches fitting, that is most of the work.
- **The step is a set in memory**: it pays where it is the small part.
- **There is no option to name an order.** An order written into a model does not follow its data, nor a better
  planner; a step is plain code, read as the model reads.

## Deriving sets from sets

A set is read-only (`begin`/`end`/`size`, range-for, and read-only algorithms). Instead you **derive**. Copying is
O(1) and shares the rows.

| Member | What it does | AMPL |
|---|---|---|
| `S.where(p, q)` | Rows that pass every predicate | `{(m,i) in S: p and q}` |
| `S.select<A, B>()` | Column projection (duplicates removed). Picking every column in a different order reorders the columns | `setof {(m,i,t) in S} (m,i)` |
| `S.select(expr)` | Each row as `expr` makes it (duplicates removed) | `setof {(i,j) in S} (j,i)` |
| `S.orderBy(key)`, `S.orderBy(key, descending)` | Stable sort by what `key` returns, ascending (the default) or descending ([Ordered sets](#ordered-sets--ordered)) | SQL's `ORDER BY ... ASC`/`DESC`, not AMPL's `ordered by` |
| `S.groupBy<A>()` | Grouping (next section) | slices |
| `S.membership()` | O(1) membership predicate, to put in another set's `where` | `(m,i) in S` |
| `S.ordered()` | Positions and neighbours in the set's order ([Ordered sets](#ordered-sets--ordered)) | an ordered set |
| `S.ordered<A>()` | The same within each group of rows that agree on `A` | `next(t, S[a])` |

| Free function | |
|---|---|
| `dreamor::unite(A, B)` | A ∪ B |
| `dreamor::intersect(A, B)` | A ∩ B |
| `dreamor::difference(A, B)` | A \ B |
| `A == B` | The same rows, in any order (`std::ranges::equal(A, B)` compares the order too) |

```cpp
const auto sPEAK_PRODUCE  = sPRODUCE.where(is_peak);                  // production rows in peak-demand periods
const auto sMACHINE_ITEM  = sPRODUCE.select<cMachine, cItem>();  // (machine, item) only
const auto in_routes      = sROUTES.membership();
```

The `Membership` returned by `membership()` is a predicate object, so you may also call it directly, passing the
columns separately (`in_routes(cMachine{m}, cItem{i})`) or a single row tuple (`in_routes(row)`).

`select(expr)` is the general `setof`. `expr` takes the columns it reads, as a predicate does, and returns the new
row's columns: one `Col`, or a `std::tuple` of them. A value moves into another column only where that column is
written out. `cTo` does not turn into `cFrom` on its own, so two columns of the same value type cannot trade
places unnoticed.

```cpp
// (j,i) for every (i,j)
const auto sREVERSED = sEDGES.select([](cFrom i, cTo j) { return std::tuple<cFrom, cTo>{j, i}; });
const auto sTAILS    = sNODES.select([](cNode n) { return cFrom{n}; });  // the nodes as arc tails
```

## Grouping — groupBy

```cpp
const auto producedByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();

for (const auto& [item, period, rows] : producedByItemPeriod) { ... }     // every group (existing keys only)
const auto producingRows = producedByItemPeriod.slice(i, t);            // a specific key (empty group if absent)

for (const auto& [item, period, rows] : sPRODUCE.groupBy<cItem, cPeriod>()) { ... }  // no name needed if you only iterate
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

## Ordered sets — ordered

A set's order is the order its rows were made in. `orderBy` gives it another, and `ordered()` reads it as an ordered
set: positions, neighbours, ends - AMPL's `ord`, `next`, `first`, over the whole set or within each group of rows.

### The order a set has

Rows come out in generation order, and which column varies fastest is not fixed ([Building sets](#building-sets--makeset)).
Where the order carries meaning - the periods a stock moves through, the steps a chain climbs - make it explicit:
build the set from a source in that order, or sort it with `orderBy`, a stable sort by what its key returns, ascending
by default or `dreamor::descending` - SQL's `ORDER BY ... ASC`/`DESC`.

The key of `orderBy` takes the columns it reads, as a predicate does, and returns what to compare - one value, or a
`std::tuple` for several. Give it an explicit key (an id, a name): a key that falls back on a pointer orders by address,
which varies between runs.

```cpp
const auto name_of       = [](cItem i) { return i->name; };
const auto sBY_NAME      = sITEMS.orderBy(name_of);                       // ORDER BY name
const auto sBY_NAME_DOWN = sITEMS.orderBy(name_of, dreamor::descending);  // ORDER BY name DESC
```

dreamor has no counterpart of AMPL's `ordered by T`, a set that takes its order from another ordered set: sort by a
key instead. A set of numbers that AMPL would order `by Reals` is, in a model of entities, a set of entities ordered by
the number they hold - `orderBy(value_of)`, or `orderBy(value_of, dreamor::descending)` for `by reversed Reals`.

### Positions and neighbours — ordered()

```cpp
const auto orderedPeriods = sPERIODS.ordered();
orderedPeriods.ord(t);         // ord(t): the 0-based position, or nullopt for a non-member
orderedPeriods.next(t);        // next(t): a pointer to the next row, nullptr after the last
orderedPeriods.next(t, 2);     // two steps on; negative steps walk back
orderedPeriods.prevOf(t);      // prev(t) where the domain promises one: the row itself
orderedPeriods.nextCyclic(t);  // nextw(t): from the last back to the first
orderedPeriods.isLast(t);      // ord(t) = card(PERIODS)
orderedPeriods.first();        // first(PERIODS); last() and at(position) likewise
```

`ordered()` is a positional view (`Ordered`) over the deduplicated set; positions count from 0. Each neighbour comes
two ways: `next`/`prev` return a pointer to the row, or `nullptr` past either end or for an element not in the set;
`nextOf`/`prevOf` return the row itself, for a domain that has the neighbour (below) - a debug build stops if it is
not there. A lookup names an element by its columns, in order, or by one row of the set. The lookups hand out
pointers into the `Ordered`, so they work on a named one only (on a temporary they are a compile error).

### Within groups — ordered<A>()

```cpp
const auto periodsOfMachine = sRUN.ordered<cMachine>();
periodsOfMachine.next(m, t);    // next(t, S[m]): the period after t among m's, nullptr after m's last
periodsOfMachine.isLast(m, t);  // ord(t, S[m]) = card(S[m])
periodsOfMachine.first(m);      // first(S[m]): the row (m, t) of m's first period; last(m) likewise
```

`ordered<A>()` orders within each group of rows that agree on `A` - AMPL's indexed collection of ordered sets,
`S[a]`: a row's neighbours are the rows of its own group, in the set's order, so a walk stops (or, cyclic, wraps) at
its group's ends, `ord` is the position within the group, and `isFirst`/`isLast` are the group's ends. `first(a)` and
`last(a)` give a group's first and last row; the group must have one (a debug build stops if not - run over the
groups the set has, `groupBy`). `first()`, `last()` and `at()`, positions of the whole set, are not there.

### A neighbour in a row, or a row where there is one

A neighbour decides either a term of a row or whether the row is there at all. A term is the row's own
if-then-else, as AMPL's `(if ord(t) > 1 then Stock[i,prev(t)])`: the README's inventory balance adds the previous
period's stock when `prev(t)` gives a row. A row that is there only where a neighbour is runs over that domain,
as AMPL's `subject to C {(i,t) in STOCK: ord(t) > 1}`: the condition goes into the set the loop runs over, not
into an `if` around the row, and the row reads its neighbour with `prevOf(t)` / `nextOf(t)`, which return the row
itself - the domain promises it is there.

```cpp
const auto not_first = [&orderedPeriods](cPeriod t) { return !orderedPeriods.isFirst(t); };
for (const auto& [i, t] : sSTOCK.where(not_first)) {  // {(i,t) in STOCK: ord(t) > 1}
  const cPeriod prevPeriod = std::get<cPeriod>(orderedPeriods.prevOf(t));
  /* a row on t and prevPeriod */
}
```

### AMPL's ordered sets in dreamor

| AMPL | dreamor (`orderedPeriods`, `periodsOfMachine` as above) |
|---|---|
| `set S ordered;` (the data's order) | every set: generation order |
| `ordered by Reals`, `ordered by reversed Reals` | `S.orderBy(value_of)`, `S.orderBy(value_of, dreamor::descending)` |
| `ordered by T` | none: sort by a key |
| `ord(t, S)` (from 1) | `orderedPeriods.ord(t)` (from 0) |
| `next(t, S)`, `prev(t, S)`, `next(t, S, 2)` | `next(t)`, `prev(t)`, `next(t, 2)` (a pointer); `nextOf(t)`, `prevOf(t)` (the row) |
| `nextw(t, S)`, `prevw(t, S)` | `nextCyclic(t)`, `prevCyclic(t)` |
| `first(S)`, `last(S)`, `member(k, S)` | `first()`, `last()`, `at(k - 1)` |
| `next(t, S[m])`, `ord(t, S[m])` | `periodsOfMachine.next(m, t)`, `periodsOfMachine.ord(m, t)` |
| `first(S[m])`, `last(S[m])` | `periodsOfMachine.first(m)`, `periodsOfMachine.last(m)` |
| `{t in S: ord(t) > 1}` | `S.where(not_first)`, with `isFirst` |

## Variable families — VarTable

```cpp
const dreamor::concert::VarTable<KeyCols...> vName{env, set, lb, ub, type, "Name"};
```

- Built **all at once, from a set**. After construction its structure (which keys exist, their order, the mapping
  from key to variable) never changes. There is no `add` or `create`.
- `lb` and `ub` are each **a number or a column function**. Functions follow the same rule as `where` predicates.
  ```cpp
  const auto max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; };
  const dreamor::concert::VarTable<cMachine, cItem, cPeriod> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
  ```
- Defaults when omitted: `lb = 0`, `ub = IloInfinity`, `ILOFLOAT`, no name.
- Semi-continuous variables: `dreamor::concert::VarTable<...> vBatch{dreamor::concert::semiContinuous, env, sBATCH, sclb, ub, ILOFLOAT, "Batch"};`
- Names are `name_key0_key1...` (`Yield_M0_I3_2`). Key tokens come from the `keyName()` hook (or `primaryKey()`).
  `nullptr` means no name. A row is named the way its variables are with `dreamor::exprName("Cap", m, t)` - the same
  tokens, in a buffer of the thread's, with no format string to read; a column under `std::format` (or a
  `FormatBuffer`'s `format`) prints the same token, for a name of another shape.
- A VarTable takes its set in its constructor, so the set must be built first. When you bundle a model into a struct,
  either declare the sets as members before the VarTables (members are constructed in declaration order) or put them
  in a base class (a base is constructed before the members). The example's `Sets` / `Model` takes the base class.

| Use | |
|---|---|
| `vYield(m, i, t)` / `vYield(row)` | The variable. `std::out_of_range` for a key not in the set |
| `vYield.contains(...)` | Whether the key exists |
| `vYield.size()` | Number of variables |
| `vYield.keys()` | The table's set |
| `for (const auto& [row, yieldVar] : vYield)` | (row, variable) in set order — the column order given to the model |
| `vYield.solution(cplex)` | Read the solution (below) |

**Tightening bounds later** needs no new API: pick rows with a set and change them with the solver API (below is
Concert; for MathOpt, `model.set_upper_bound(vYield(row), 0)`; for HiGHS,
`env.highs().changeColBounds(vYield(row).index(), 0, 0)`).

```cpp
const auto in_maintenance = [](cMachine m, cPeriod t) { return m->name == "M0" && t.get() == 2; };
for (const auto& row : vYield.keys().where(in_maintenance)) {
  vYield(row).setUB(0);
}
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
dreamor::concert::sumOf<cMachine>(env, machines, term).build()       // a plain container as a one-column set
```

- `rows` is a set or a group (`RowSlice`).
- `build()` is kept on purpose. Until `build()`, `sumOf(...)` is a builder, so `where()` and `split()` can be chained;
  using it without `build()` in a comparison, an assignment, `+=`, or an objective stops compilation with one
  sentence telling you to add `build()`.
- A term takes only the columns it reads and returns that row's term. Coefficients are computed from the entities
  (no separate coefficient arrays).
  ```cpp
  const auto setup_cost = [&](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
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
so there is nothing to manage, and so is HiGHS's `HighsLinearExpression`. MathOpt's and HiGHS's `sumOf` take no first
argument: their expressions need no environment.

## Constraints

Constraints use **the solver API as is**. Below is Concert; for MathOpt it is `model.AddLinearConstraint(...)`; for
HiGHS, whose own API has no expressions, it is `dreamor::highs::addConstr(env, row, name)`, highspy's `addConstr`. The
library has no constraint tables (for why, see [What dreamor does not do](philosophy.md#what-dreamor-does-not-do)).

```cpp
// A constraint family over a set: one per group
for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
}

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

## Names

A model names its variables and rows by one rule, `<family>_<key0>_<key1>...`, each key by its token (`keyName()`,
else `primaryKey()`, else the value). Two tools cover it; the other two are for the exceptions.

| Tool | Who uses it | What it does |
|---|---|---|
| `VarTable` | every model | names its variables itself (`Yield_M1_I3_2`): nothing to call |
| `dreamor::exprName(family, keys...)` | whatever else needs a name | the same rule for one name - a row, or a variable made outside a set: `model.add(load <= cap).setName(dreamor::exprName("Capacity", m, t))`. A key without one is a literal (`"SetupBudget"`) |
| `general::FormatBuffer<N>::format` | rarely | a name of another shape (`x[1,2]`, a number as `{:.2f}`), from a format string; a column prints as its token |
| `general::NameBuilder<N>` | more rarely | a name put together part by part - down a tree, say, a level at a time; what `VarTable` and `exprName` are built on |

`exprName` and `format` build in a buffer of the thread's and return a pointer valid until the thread's next call:
hand it straight to `setName`, `AddLinearConstraint` or `addConstr`, which copy it. `exprName` reads no format
string - `std::format` reads one on every call - so a model that names tens of thousands of rows takes a fraction of
the time.

## Reading solutions

```cpp
const auto yieldPlan = vYield.solution(cplex);   // the whole table with one getValues
yieldPlan(m, i, t);                          // a value by key
for (const auto& [row, qty] : yieldPlan) { ... } // in set order
```

- The result (`ValueTable`) holds only doubles, so it stays usable after `cplex` or `env` has ended. It is the same
  type regardless of backend.
- A `ValueTable` can also be built by hand on a set: `dreamor::ValueTable<cMachine, cPeriod>
  hours{sRUN, available_hours}` gives each row of `sRUN` a value - a number, or a callable over the row's columns, as
  a `VarTable`'s bounds are - and is read the same way, by key or row, `std::out_of_range` outside the set. It is a
  column callable itself, so it goes where a callable goes: a `VarTable`'s bound (`VarTable{env, sRUN, 0, hours}`),
  another `ValueTable`'s value. That makes it AMPL's `param {S}` too, and nothing forbids that use; but a parameter
  is better a field of its entity, or a named callable when an expression defines it
  ([Philosophy](philosophy.md#design-principles)), and a `ValueTable` is mainly for what a solve produces and what
  an algorithm computes from it.
- Looking up a key not in the set (`yieldPlan(row)`) throws `std::out_of_range` instead of returning 0 (0 would be
  indistinguishable from "a variable whose value is 0").
- Concert's `solution()` accepts any algorithm that has `getEnv()` and `getValues(IloNumArray, IloNumVarArray)`. The
  temporary arrays it creates internally are cleaned up by itself.
- MathOpt's `solution()` takes the `SolveResult` returned by `math_opt::Solve` (`vYield.solution(*result)`). It
  throws `std::logic_error` if there is no feasible solution, because MathOpt itself would stop the process in that
  case (a CHECK failure).
- HiGHS's `solution()` takes the `HighsEnv` (`vYield.solution(env)`) and reads the solution its `Highs` holds. It
  throws `std::logic_error` if there is no primal solution, rather than return values HiGHS left from before.

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

This section does not apply to MathOpt or HiGHS. `math_opt::Model` and `LinearExpression`, `HighsEnv` and
`HighsLinearExpression` are ordinary C++ objects that are cleaned up when they go out of scope, and there is no call
like `end()`. A variable (`math_opt::Variable`, `HighsVar`) is a value that points at its model, so use it only while
the model is alive.

## HiGHS's environment — HighsEnv

HiGHS's own C++ API takes a model as a whole (`passModel`) or a change at a time (`addRow`, `changeColBounds`, ...),
and pays for every change: it clears what it derived from the model and checks the new matrix. `HighsEnv` holds both
the model being written and the `Highs` that solves it, and decides when one reaches the other.

- **Written, then passed once.** `VarTable`, `addConstr` and `setObjective` write into the environment's model, and
  the `Highs` gets all of it in one `passModel` the first time it is reached: `env.highs()`, `minimize` or `maximize`,
  a `solution()`. From then on each goes to the `Highs` as it is written, as Concert's model reaches an `IloCplex`
  that has extracted it. Reaching it first only leaves every row to the incremental path: nothing is lost but time.
- **Options touch no model.** `HighsEnv{options}` takes a `HighsOptions`, so a quiet log or a time limit holds for
  the `passModel` too; `env.highs().setOptionValue(...)` works as well, after the model has reached it.
- **The rest is HiGHS's own API** on `env.highs()`: `run()`, `getInfo()`, `writeModel`, `changeColBounds`.
- **A `HighsVar` knows its environment.** One expression of two environments' variables, or a row of one handed to
  another, is a broken contract: a debug build stops on it.
- A HiGHS call that fails throws `HighsError`, which carries the call and its `HighsStatus`; a model HiGHS refuses
  throws from that first reach (`"passModel"`).

## Large models: transparent huge pages

**Tip: turn transparent huge pages on for large models.** A model of millions of nonzeros spends much of the time
before the solver starts in page faults: every 4 KiB page of the arrays it fills is a fault the first time it is
touched, and every page a TLB entry. Huge pages (2 MiB) take hundreds of those at once. Linux decides it in
`/sys/kernel/mm/transparent_hugepage/enabled`: under `always` every large allocation gets them, under `madvise`
(Ubuntu's default) only memory that asks, and glibc's `malloc` asks for its heap when told to:

```sh
GLIBC_TUNABLES=glibc.malloc.hugetlb=1 ./model
```

On p-median at 20,000 locations (2,000,000 columns), from the start of the run to the solver's start, it took the
page faults down about thirtyfold and the time down by a third to nearly two thirds, whatever wrote the model
(GCC 13, AMD Ryzen 7 5700X, Linux 7.0, `madvise`):

| Model written through | THP off | THP on |
|---|---|---|
| CPLEX's Callable Library, arrays | 362 ms | 134 ms |
| Concert, by hand | 1,258 ms | 842 ms |
| HiGHS, arrays and one `passModel` | 1,635 ms | 908 ms |
| `dreamor::highs` | 1,738 ms | 1,003 ms |

Neither CPLEX nor HiGHS asks for huge pages itself; their large arrays come from the same `malloc`. The
measurements elsewhere in these documents and under `bench/` leave THP off, the default, so that they hold for a
machine left as it came.

## Compile cost of wide sets

One translation unit that builds a set of N columns, filters it, groups it, puts a
`VarTable` on it and sums over it (`-std=c++20 -O2 -c` without `NDEBUG`, so with the debug build's contract checks,
which `-DNDEBUG` compiles out; CPU time and peak memory; AMD Ryzen 7 5700X, bare metal, boost off, Linux 7.0, 2026-11-01).
`python3 tools/measure_compile_cost.py [compiler ...]` re-measures the table.

| Columns | g++ 13.3 | clang++ 18.1 | clang++ 23.1 |
|---|---|---|---|
| 4 | 3.9 s, 323 MB | 3.8 s, 220 MB | 3.8 s, 235 MB |
| 16 | 4.6 s, 331 MB | 4.7 s, 249 MB | 4.6 s, 261 MB |
| 32 | 5.6 s, 394 MB | 6.1 s, 305 MB | 6.0 s, 313 MB |
| 63 | 9.4 s, 574 MB | 11.1 s, 473 MB | 10.6 s, 475 MB |
| 64 | 9.6 s, 585 MB | 11.4 s, 481 MB | 10.8 s, 484 MB |
