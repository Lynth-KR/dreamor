# dreamor 0.7 API reference

Contents: [Columns](#columns) · [Building sets](#building-sets) · [IndexSet](#indexset) · [Grouped and RowSlice](#grouped-and-rowslice) ·
[Ordered](#ordered) · [Membership](#membership) · [Concert backend](#concert-backend-dreamorconcert) ·
[MathOpt backend](#mathopt-backend-dreamormathopt) · [HiGHS backend](#highs-backend-dreamorhighs) ·
[Common to all backends](#common-to-all-backends) ·
[ValueTable](#valuetable)

The headers in `include/dreamor/` are authoritative; the usage examples in their comments are compiled by
`tests/test_docs.cpp`.

## Columns

`dreamor/index_set.hpp`, namespace `dreamor`.

| | |
|---|---|
| `Col<T, "name">` | Column type. Implicit `Col(T)`, `operator T()`, `get()`, `->` and `*` for pointer `T`, `==`, default construction |
| `Col<...>::kName` | The name, `std::string_view` |
| `ColumnName` | Type of the name template argument (a string literal) |
| `keyName(Col)` | The value's `keyName` if one is found (same namespace as the entity), otherwise the value itself. A variable's name and a column under `std::format` use, for an entity pointer with no `keyName`, the entity's `primaryKey`; the token must be `std::format`-able |
| `std::formatter<Col<...>>` | A column prints as its name token: `std::format("Cap_{}_{}", i, j)` names a row the way VarTable names variables. No format spec; a column with no token has none |
| `std::hash<Col<...>>` | Columns can key `std::unordered_map` |
| `CallableColumns<F>` | Specialization point: `template <> struct dreamor::CallableColumns<F> { using type = std::tuple<Cols...>; };` for a function object with a template `operator()` |

Two columns over the same `T` with different names are different types (`cFrom`, `cTo` over `const Node*`).

## EntityVector

`entity_vector.hpp` (also through `index_set.hpp`). A container that holds each entity once by its primary key, declared by ADL next to
the type: `inline const std::string& primaryKey(const Machine& m) { return m.name; }` (a composite key returns a
`std::tuple`). `dreamor::Entity<T>` is that requirement.

| Member | Result |
|---|---|
| `reserve(n)` | room for `n` elements; before `freeze()` |
| `insert(entity)`, `emplace(args...)` | `std::pair<const T*, bool>`: the element with that key by its address, whether it went in (as `std::set`); the address lasts as a `std::vector` element's does. Before `freeze()` |
| `sort()`, `sort(comp)` | primary-key order (`std::less`), or `comp`'s as `std::sort` takes it; returns the container. Before `freeze()` |
| `freeze()`, `frozen()` | done filling; read only from here on |
| `find(key)`, `contains(key)` | by primary key; `find` gives the element's address, `nullptr` when absent. After `freeze()` |
| `operator[]`, `at`, `front`, `back`, `begin`, `end`, `cbegin`, `cend` | const access, the container's order. After `freeze()` |
| `size`, `empty` | any time |
| `value_type`, `key_type`, `size_type`, `const_iterator` (= `iterator`) | member types, as the standard containers name them; `key_type` is what `primaryKey()` returns |
| `modify(entity, fn)` | `fn(T&)` on one element in place; the key must not change (debug builds check) |

Filled, frozen, then read; a debug build stops on either side crossed. Once frozen, elements never move (a
`std::vector`), nothing is erased, copying is deleted, moving keeps elements in place. A
container source in `makeSet` like any other. Without `keyName`, an entity column's variables are named by
`primaryKey` (debug builds log it once per column). Sets are snapshots: `modify` after building does not change them.

## Building sets

`dreamor/index_set.hpp`.

```cpp
dreamor::makeSet<Cols...>(sources...)   // one source per column; a set source covers all its columns
    .where(p...)                        // ANDed, chainable; each evaluated as soon as its columns are bound
    .build();                         // → IndexSet<Cols...>
```

| Source | Example | Meaning |
|---|---|---|
| Container (lvalue) | `machines` | Every value; elements may be pointers, entities, or smart pointers — all give a pointer column |
| Interval | `dreamor::interval::closed(1, T)` | The integers 1..T (AMPL's `1..T`); `open`, `closedOpen`, `openClosed` leave out an end or both; values take the column's integer type; passed as a temporary |
| Dependent source | `[](cMachine m) { return m->items; }` | Values depending on earlier columns; for an entity column a range of pointers, for a value column any range |
| Existing set | `makeSet<cPeriod>(sROUTES, periods)` | Fills that set's columns at once. Set sources sharing a column are joined on it (`makeSet<>(sROUTES, sOPEN)`): the later one is looked up, not scanned. A column named in `makeSet<...>` is always new |

Row order is generation order; which column varies fastest is unspecified. Use `orderBy` when order matters.
Precondition: no source holds an element twice (a container, a dependent source's range, a set), and an entity is
one object in one container; breaking it is undefined behavior, and a debug build aborts on it with a message
(`makeSet: source N holds one element twice ...`). A set therefore never holds a row twice.
A set has at most 64 columns, spread sources included; 65 stops at compile time.

## IndexSet

`dreamor/index_set.hpp`. Read-only, random-access, default-constructible (empty), O(1) copy sharing rows.

| Member | Result | AMPL |
|---|---|---|
| `begin()`, `end()`, `size()`, `empty()` | iteration | |
| `where(p...)` | `IndexSet` of rows passing all predicates | `{(m,i) in S: p and q}` |
| `select<Cols...>()` | `IndexSet`, projection, duplicates removed; all columns in another order = reorder | `setof {...} (m,i)` |
| `select(expr)` | `IndexSet` of what `expr` returns per row (one `Col` or a `std::tuple` of them), duplicates removed. `expr` takes the columns it reads; a value changes column only where the new column is written: `std::tuple<cFrom, cTo>{j, i}` | `setof {(i,j) in S} (j,i)` |
| `orderBy(key)`, `orderBy(key, descending)` | `IndexSet`, stable sort by what `key` returns - SQL's `ORDER BY`, not AMPL's `ordered by`; the key takes the columns it reads; `dreamor::ascending` is the default | |
| `groupBy<Cols...>()` | `Grouped` | slices |
| `membership()` | `Membership` | `(m,i) in S` |
| `ordered()` | `Ordered` | ordered set |
| `orderedWithin<A...>()` | `Ordered` within each group of rows that agree on `A...` | `next(t, S[a])` |

Free functions: `dreamor::unite(A, B)` (∪), `intersect(A, B)` (∩), `difference(A, B)` (\). `A == B`: the same rows in
any order, as `std::unordered_set`'s; `std::ranges::equal(A, B)` compares the order too.

In generic code where the set type is a template parameter: `sANY.template select<A>()`.

## Grouped and RowSlice

`dreamor/index_set.hpp`.

| `Grouped` | |
|---|---|
| range-for | groups in first-occurrence order of the key; each `Group` unpacks as `[keyCols..., rows]`; bind one name to use `.key` and `.rows` |
| `slice(cols...)`, `slice(key)` | `RowSlice` of that key, empty if absent — on a named `Grouped` only |
| `contains(cols...)` | also accepts a `key` |
| `size()`, `empty()` | number of groups |
| `at(g)` | the g-th `Group` (key and rows), `std::out_of_range` if out of range — on a named `Grouped` only |
| `regrouped()` | new `IndexSet` with rows gathered group by group |
| `key_type`, `Group{key, rows}` | types; `key` values come from `Group::key` |

Within a group, rows keep the set's order.

`RowSlice`: `begin`, `end`, `size`, `empty`, `operator[]`, `front`, `back`, default-constructible (empty). It is a
random-access view into the set's rows (no copies), valid while the `Grouped` lives, and works with ranges algorithms.

## Ordered

`dreamor/index_set.hpp`. A positional view over the set - or, from `orderedWithin<A...>()`, within each group
of rows that agree on `A...`: walks stop (cyclic: wrap) at the group's ends, `positionOf`/`isFirst`/`isLast` are within the
group, `first(a...)`/`last(a...)` give a group's first and last row (AMPL's `first(S[a])`), and `first()`/`last()`/`at()`
stop on a sentence. Every lookup is on a named `Ordered` only, and accepts either
the columns or one row.

| | |
|---|---|
| `positionOf(cols...)` | `std::optional<std::size_t>`, 0-based (AMPL's `ord` minus one), empty if absent |
| `next(cols...)`, `next(cols..., steps)`, `prev(...)` | `const Row*`, `nullptr` past the boundary or for a non-member; negative steps walk backwards |
| `nextOf(...)`, `prevOf(...)` | `const Row&`, the neighbour itself, which must be there (a domain filtered with `isFirst` / `isLast`); a debug build stops if it is not, a release build does not check |
| `nextCyclic(...)`, `prevCyclic(...)` | wrap from the end to the start |
| `first()`, `last()`, `at(pos)` | rows (the whole set only) |
| `first(cols...)`, `last(cols...)` | under `orderedWithin<A...>()`, the group's first and last row; the group must have one (a debug build stops if not) |
| `isFirst(cols...)`, `isLast(cols...)`, `contains(cols...)` | tests |
| `size()`, `empty()`, `members()` | size, the set |

Read a column from a returned pointer with `std::get<cPeriod>(*prevPeriod)`, from a returned row with
`std::get<cPeriod>(orderedPeriods.prevOf(t))`.

## Over rows

`dreamor/index_set.hpp`, namespace `dreamor`. Rows: a set, a group's rows, any range of rows; callables take columns.

| | |
|---|---|
| `allOf/anyOf/noneOf(rows, pred)` | `bool` (forall / exists / not exists), stops early |
| `countIf(rows, pred)` | `std::size_t`, `card {rows: pred}`, no set made |
| `maxOf/minOf(rows, value)` | the value; rows not empty (precondition, checked in debug) |
| `sumValue(rows, value)` | sum of data, `0` for none (`sumExpr` is the solver's expression) |
| `maxElement/minElement(rows, value)`, `findIf(rows, pred)` | `const Row*`, first of equals, `nullptr` if none; rows must outlive the call (a named set) |

## Membership

`dreamor/index_set.hpp`. `in_routes(cols...)` or `in_routes(row)` → `bool`, O(1). Also usable as a `where` predicate.

## Concert backend (`dreamor::concert`)

`concert.hpp`.

```cpp
VarTable<Keys...>{const IloEnv& env, const IndexSet<Keys...>& set,
                  Lb lb = 0, Ub ub = IloInfinity, IloNumVar::Type type = ILOFLOAT, const char* name = nullptr};
VarTable<Keys...>{semiContinuous, const IloEnv& env, const IndexSet<Keys...>& set,
                  Lb sclb, Ub ub, IloNumVar::Type type = ILOFLOAT, const char* name = nullptr};
```

- `lb`, `ub`, `sclb` are numbers or column callables.
- `type` is `ILOFLOAT`, `ILOINT`, or `ILOBOOL`, not `bool`.
- `name` is a `const char*` (a literal or `.c_str()`), not a `std::string`. `nullptr` leaves variables unnamed;
  otherwise variables are named `<name>_<key0>_<key1>...`.
- `SemiContinuous` is the tag type and `semiContinuous` its value.
- `solution(algorithm)` → `dreamor::ValueTable<Keys...>`. It accepts any object with `getEnv()` and
  `getValues(IloNumArray, IloNumVarArray)`, makes one `getValues` call, and cleans up its temporary arrays.
- A VarTable does not touch Concert when destroyed; `env.end()` releases everything.

```cpp
sumExpr(IloEnv env, rows, term)            // rows: IndexSet or RowSlice; term: column callable or a VarTable
sumExpr<Col>(IloEnv env, container, term)  // a plain lvalue container as a one-column set
    .where(p...)                         // only rows where all p hold; chainable
    .whereEach(p1, ..., pN)                  // at most once; one pass, N sums, term computed once per row
    .build();                            // IloExpr, or std::array<IloExpr, N> after split
```

- With a VarTable as the term, the table picks its key columns out of each row, so the rows may have extra columns.
- `IloExpr` accepts quadratic terms.
- The caller owns the returned `IloExpr`. It can be `end()`ed once the constraint is added (the constraint holds a
  deep copy).
- Concert has no `IloExpr(number)` constructor. Compare numbers directly, and use variables unwrapped.

## MathOpt backend (`dreamor::mathopt`)

`mathopt.hpp`. `namespace math_opt = ::operations_research::math_opt;`

```cpp
VarTable<Keys...>{math_opt::Model& model, const IndexSet<Keys...>& set,
                  Lb lb = 0, Ub ub = +inf, bool is_integer = false, const char* name = nullptr};
solution(const math_opt::SolveResult&)   // → ValueTable; std::logic_error if no primal feasible solution

sumExpr(rows, term)                        // no env, no model
sumExpr<Col>(container, term)
    .where(p...).whereEach(p...).build();    // math_opt::LinearExpression; QuadraticExpression for a quadratic term
                                         // (an array after split)
```

- A binary variable is `0, 1, true`.
- Constraints: `model.AddLinearConstraint(expr, name)`. Bounds: `model.set_upper_bound(var, value)`.
- There are no semi-continuous variables.
- A quadratic term (`x * x`, `(x - 1) * (x - 1)`) makes the sum a `QuadraticExpression`, for `model.Minimize`. HiGHS
  takes no quadratic objective through MathOpt; SCIP (`kGscip`) and Gurobi do.
- `math_opt::Variable` is valid while its model lives; there is nothing to `end()`.
- Building against the OR-Tools C++ prebuilt without CMake needs `-DOR_PROTO_DLL=` and
  `-lortools -lprotobuf` plus the Abseil libraries. `ortools::ortools` in CMake carries all of that.

## HiGHS backend (`dreamor::highs`)

`highs.hpp`. `namespace hs = dreamor::highs;` HiGHS's own C++ API with highspy's expressions.

```cpp
HighsEnv env;                            // the model as written, and the Highs that solves it; not copied, not moved
HighsEnv env{options};                   // options: a HighsOptions, the Highs's before the model reaches it
env.highs();                             // the Highs: the first call hands it the model in one passModel
env.addVariable(lb = 0, ub = kHighsInf, type = HighsVarType::kContinuous,
                name = nullptr);         // → HighsVar: one variable (an epigraph, a makespan); a family is a VarTable

VarTable<Keys...>{HighsEnv& env, const IndexSet<Keys...>& set,
                  Lb lb = 0, Ub ub = kHighsInf, HighsVarType type = HighsVarType::kContinuous,
                  const char* name = nullptr};
solution(const HighsEnv& env)            // → ValueTable; std::logic_error if no primal solution

HighsCons addConstr(HighsEnv& env, Row&& row, const char* name = nullptr);   // row: expr <= b, a <= expr,
                                                                             // expr == b, (a <= expr) <= b
void setObjective(HighsEnv& env, const Objective& objective);                // replaces costs, constant, Hessian
void setObjective(HighsEnv& env, const Objective& objective, ObjSense sense);
HighsStatus minimize(HighsEnv& env, const Objective& objective);             // setObjective, then run()
HighsStatus maximize(HighsEnv& env, const Objective& objective);

sumExpr(rows, term)                        // no env
sumExpr<Col>(container, term)
    .where(p...).whereEach(p...).build();    // HighsLinearExpression; HighsQuadraticExpression for a quadratic term
```

- `HighsVar`, `HighsCons`: `index()`, `env()`. A term is `HighsLinearTerm` (`3 * x`) or `HighsQuadraticTerm`
  (`x * y`). An objective is a `HighsVar`, a term, a `HighsLinearExpression` or a `HighsQuadraticExpression`; a row
  (`HighsBoundedLinearExpression`, or the one-sided `HighsLowerBoundedLinearExpression` /
  `HighsUpperBoundedLinearExpression` that `a <= expr` / `expr <= b` make) is not one.
- A quadratic expression is compared to nothing: HiGHS takes quadratic terms only in the objective.
- `addConstr` and `setObjective` merge repeated columns and drop the ones that cancel.
- A HiGHS call that fails throws `HighsError` (`call()`, `status()`); a model HiGHS refuses throws from the first
  `env.highs()` (`"passModel"`).
- Mixing two `HighsEnv`s' variables in one expression, or one env's row in another, is a broken contract.
- Building: `highs::highs` in CMake, or HiGHS's `include/highs` and `libhighs`; the HiGHS in the OR-Tools prebuilt is
  one.

## Common to all backends

- `vYield(keys...)`, `vYield(row)`: the variable; `std::out_of_range` if the row is not in the set.
- `contains(keys...)`, `contains(row)`, `size()`, `keys()` (the table's set), `key_type` (= `std::tuple<Keys...>`).
- Range-for yields `[row, var]` in set order, which is also the column order handed to the model.
- Built once from the set. There is no `add`/`create` and no default constructor; tables are move-only.
- One variable per row: a set holds no row twice (see Building sets).
- The table's key columns must equal the set's columns in the same order.

## ValueTable

`value_table.hpp`, namespace `dreamor`, the same type for every backend: what `solution()` returns, and values on a
set built by hand - mainly what an algorithm computes from a solve. A parameter is better its entity's field, or a
named callable when an expression defines it (`docs/philosophy.md`).

- `ValueTable<Keys...>{set, value}` - a table built by hand (AMPL's `param {S}`, where a design has a reason for
  one): `value` is a number or a column callable, as a VarTable's bounds are; the set's columns must be exactly
  `Keys...`, in order. Shares the set's row index with the tables on it.
- A ValueTable is a column callable over its keys: pass it as a VarTable's bound or another ValueTable's value
  (`VarTable{env, sRUN, 0, hours}`), no lookup lambda.
- `yieldPlan(keys...)`, `yieldPlan(row)` → `double`; `std::out_of_range` if absent (never a silent 0).
- `contains`, `size()`, `keys()`, range-for `[row, value]` in set order.
- Holds only doubles, so it outlives `IloCplex`, `IloEnv`, and the `SolveResult`.

## Utilities

No solver; the library uses them itself.

- `dreamor::indexedName(family, keys...)` (`dreamor/index_set.hpp`): `"<family>_<key0>_<key1>..."`, a row named as
  VarTable names variables - columns by their token, or values - in a buffer of the thread's, no allocation, no
  format string; valid until the thread's next `indexedName`. `NameBuilder<N>` builds such a name part by part
  (`restart(root)`, `append(length, token)`, `name()`).
- `dreamor/format_buffer.hpp`: for a name of another shape, `class Model : private dreamor::FormatBuffer<256>`, then
  `model.add(expr <= cap).setName(format("Capacity_{}_{}", m, t));`. A column formats as its name token; `format`
  is static, on a buffer of the thread's, so a `const` member function calls it and threads call it at once; the
  returned `const char*` is valid until the thread's next `format` through any `FormatBuffer<N, Policy>`. Too long a name throws (`kThrow`, the default) or logs and
  clears (`OnFormatOverflow::kLogAndClear`).
- `dreamor/logger.hpp`: `DREAMOR_LOG_INFO/WARNING/ERROR/FATAL(fmt, args...)` (`std::format`, checked
  at compile time; `FATAL` aborts in every build), `DREAMOR_DEBUG_INFO/ERROR/FATAL` (gone under `NDEBUG`),
  `dreamor::logger::setLevel(Level::kWarning)`, `dreamor::logger::FileSink sink{"model.log"};` (this thread, this
  scope).

