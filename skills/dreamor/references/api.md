# dreamor 0.2 API reference

Contents: [Columns](#columns) · [Building sets](#building-sets) · [IndexSet](#indexset) · [Grouped and RowSlice](#grouped-and-rowslice) ·
[Ordered](#ordered) · [Membership](#membership) · [Concert backend](#concert-backend-dreamorconcert) ·
[MathOpt backend](#mathopt-backend-dreamormathopt) · [Common to both backends](#common-to-both-backends) ·
[ValueTable](#valuetable)

The headers in `include/dreamor/` are authoritative; the usage examples in their comments are compiled by
`tests/test_docs.cpp`.

## Columns

`index_set/column.hpp`, namespace `dreamor`.

| | |
|---|---|
| `Col<T, "name">` | Column type. Implicit `Col(T)`, `operator T()`, `get()`, `->` and `*` for pointer `T`, `==`, default construction |
| `Col<...>::kName` | The name, `std::string_view` |
| `ColumnName` | Type of the name template argument (a string literal) |
| `keyName(Col)` | Name token: the value's `keyName` if one is found (same namespace as the entity), else the entity's `primaryKey`, else the value itself (must be `std::format`-able) |
| `std::hash<Col<...>>` | Columns can key `std::unordered_map` |
| `PredicateColumns<F>` | Specialization point: `template <> struct dreamor::PredicateColumns<F> { using type = std::tuple<Cols...>; };` for a function object with a template `operator()` |

Two columns over the same `T` with different names are different types (`FromCol`, `ToCol` over `const Node*`).

## EntityVector

`index_set/entity_vector.hpp`. A container that holds each entity once by its primary key, declared by ADL next to
the type: `inline const std::string& primaryKey(const Machine& m) { return m.name; }` (a composite key returns a
`std::tuple`). `dreamor::Entity<T>` is that requirement.

| Member | Result |
|---|---|
| `insert(entity)`, `emplace(args...)` | `std::pair<const_iterator, bool>`: the element with that key, whether it went in (as `std::set`) |
| `find(key)`, `contains(key)` | by primary key; `find` gives `end()` when absent |
| `operator[]`, `at`, `front`, `back`, `begin`, `end`, `size`, `empty` | const access, insertion order |
| `modify(entity, fn)` | `fn(T&)` on one element in place; the key must not change (debug builds check) |

Elements never move (`std::deque`), nothing is erased, copying is deleted, moving keeps elements in place. A
container source in `makeSet` like any other. Without `keyName`, an entity column's variables are named by
`primaryKey` (debug builds log it once per column). Sets are snapshots: `modify` after building does not change them.

## Building sets

`index_set/set.hpp`.

```cpp
dreamor::makeSet<Cols...>(sources...)   // one source per column; a set source covers all its columns
    .where(pred)                        // repeatable; evaluated as soon as its columns are bound
    .collect();                         // → SetOf<Cols...> = IndexSet<std::tuple<Cols...>>
```

| Source | Example | Meaning |
|---|---|---|
| Container (lvalue) | `machines` | Every value; elements may be pointers, entities, or smart pointers — all give a pointer column |
| Dependent source | `[](MachineCol m) { return m->items; }` | Values depending on earlier columns; must return a range of pointers |
| Existing set | `makeSet<PeriodCol>(sROUTES, periods)` | Fills that set's columns at once. Set sources sharing a column are joined on it (`makeSet<>(sROUTES, sOPEN)`): the later one is looked up, not scanned. A column named in `makeSet<...>` is always new |

Row order is generation order; which column varies fastest is unspecified. Use `orderBy` when order matters.
Precondition: no source holds an element twice (a container, a dependent source's range, a set), and an entity is
one object in one container; breaking it is undefined behavior, and a debug build aborts on it with a message
(`makeSet: source N holds one element twice ...`). A set therefore never holds a row twice.
A set has at most 64 columns, spread sources included; 65 stops at compile time.

## IndexSet

`index_set/indexed.hpp` (implementation in `set.hpp`, `algebra.hpp`, `grouped.hpp`, `ordered.hpp`). Read-only,
random-access, default-constructible (empty), O(1) copy sharing rows.

| Member | Result | AMPL |
|---|---|---|
| `begin()`, `end()`, `size()` | iteration | |
| `where(p...)` | `IndexSet` of rows passing all predicates | `{(m,i) in S: p and q}` |
| `select<Cols...>()` | `IndexSet`, projection, duplicates removed; all columns in another order = reorder | `setof {...} (m,i)` |
| `select(expr)` | `IndexSet` of what `expr` returns per row (one `Col` or a `std::tuple` of them), duplicates removed. `expr` takes the columns it reads; a value changes column only where the new column is written: `std::tuple<FromCol, ToCol>{j, i}` | `setof {(i,j) in S} (j,i)` |
| `orderBy(key)` | `IndexSet`, stable sort by `key(row)`; the only place a generic lambda is allowed | |
| `groupBy<Cols...>()` | `Grouped` | slices |
| `membership()` | `Membership` | `(m,i) in S` |
| `ordered()` | `Ordered` | ordered set |

Free functions: `dreamor::unite(A, B)` (∪), `intersect(A, B)` (∩), `difference(A, B)` (\).

In generic code where the set type is a template parameter: `sANY.template select<A>()`.

## Grouped and RowSlice

`index_set/grouped.hpp`.

| `Grouped` | |
|---|---|
| range-for | groups in first-occurrence order of the key; each `Group` unpacks as `[keyCols..., rows]`; bind one name to use `.key` and `.tuples` |
| `slice(cols...)`, `slice(key)` | `RowSlice` of that key, empty if absent — on a named `Grouped` only |
| `contains(cols...)`, `sliceSize(cols...)` | also accept a `key` |
| `size()`, `empty()` | number of groups |
| `keyAt(g)`, `at(g)` | g-th key / group, `std::out_of_range` if out of range — on a named `Grouped` only |
| `regrouped()` | new `IndexSet` with rows gathered group by group |
| `Key`, `Group{key, tuples}` | types; `key` values come from `keyAt()` or `Group::key` |

Within a group, rows keep the set's order.

`RowSlice`: `begin`, `end`, `size`, `empty`, `operator[]`, `front`, `back`, default-constructible (empty). It is a
random-access view into the set's rows (no copies), valid while the `Grouped` lives, and works with ranges algorithms.

## Ordered

`index_set/ordered.hpp`. A positional view over the deduplicated set. Every lookup is on a named `Ordered` only, and
accepts either the columns or one row.

| | |
|---|---|
| `ord(cols...)` | `std::optional<std::size_t>`, 0-based, empty if absent |
| `next(cols...)`, `next(cols..., steps)`, `prev(...)` | `const Row*`, `nullptr` past the boundary or for a non-member; negative steps walk backwards |
| `nextCyclic(...)`, `prevCyclic(...)` | wrap from the end to the start |
| `first()`, `last()`, `at(pos)` | rows |
| `isFirst(cols...)`, `isLast(cols...)`, `contains(cols...)` | tests |
| `size()`, `empty()`, `members()` | size, the deduplicated set |

Read a column from a returned pointer with `std::get<PeriodCol>(*prevPeriod)`.

## Membership

`index_set/algebra.hpp`. `in_routes(cols...)` or `in_routes(row)` → `bool`, O(1). Also usable as a `where` predicate.

## Concert backend (`dreamor::concert`)

`concert.hpp`.

```cpp
VarTable<Keys...>{const IloEnv& env, const SetOf<Keys...>& set,
                  Lb lb = 0, Ub ub = IloInfinity, IloNumVar::Type type = ILOFLOAT, const char* name = nullptr};
VarTable<Keys...>{semiContinuous, const IloEnv& env, const SetOf<Keys...>& set,
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
sumOf(IloEnv env, rows, term)            // rows: IndexSet or RowSlice; term: column callable or a VarTable
sumOf<Col>(IloEnv env, container, term)  // a plain lvalue container as a one-column set
    .where(p...)                         // only rows where all p hold; chainable
    .split(p1, ..., pN)                  // at most once; one pass, N sums, term computed once per row
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
VarTable<Keys...>{math_opt::Model& model, const SetOf<Keys...>& set,
                  Lb lb = 0, Ub ub = +inf, bool is_integer = false, const char* name = nullptr};
solution(const math_opt::SolveResult&)   // → ValueTable; std::logic_error if no primal feasible solution

sumOf(rows, term)                        // no env, no model
sumOf<Col>(container, term)
    .where(p...).split(p...).build();    // math_opt::LinearExpression (an array after split)
```

- A binary variable is `0, 1, true`.
- Constraints: `model.AddLinearConstraint(expr, name)`. Bounds: `model.set_upper_bound(var, value)`.
- There are no semi-continuous variables. A quadratic term does not compile.
- `math_opt::Variable` is valid while its model lives; there is nothing to `end()`.
- Building against the OR-Tools C++ prebuilt without CMake needs `-DOR_PROTO_DLL=` and
  `-lortools -lprotobuf` plus the Abseil libraries. `ortools::ortools` in CMake carries all of that.

## Common to both backends

- `vYield(keys...)`, `vYield(row)`: the variable; `std::out_of_range` if the row is not in the set.
- `contains(keys...)`, `contains(row)`, `size()`, `keys()` (the table's set), `Key` (= `std::tuple<Keys...>`).
- Range-for yields `[row, var]` in set order, which is also the column order handed to the model.
- Built once from the set. There is no `add`/`create` and no default constructor; tables are move-only.
- One variable per row: a set holds no row twice (see Building sets).
- The table's key columns must equal the set's columns in the same order.

## ValueTable

`value_table.hpp`, namespace `dreamor`, the same type for both backends.

- `yieldPlan(keys...)`, `yieldPlan(row)` → `double`; `std::out_of_range` if absent (never a silent 0).
- `contains`, `size()`, `keys()`, range-for `[row, value]` in set order.
- Holds only doubles, so it outlives `IloCplex`, `IloEnv`, and the `SolveResult`.
