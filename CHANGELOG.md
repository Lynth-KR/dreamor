# Changelog

All notable changes to dreamor. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- `makeSet` joins set sources that share a column, as AMPL's `{(m,i) in ROUTES, (i,t) in OPEN}` does:
  `makeSet<>(sROUTES, sOPEN)`. The set visited second is looked up by the shared columns rather than scanned, so a
  join costs the two sets plus the result instead of their product.
- `S.select(expr)`, the general `setof`: each row becomes what `expr` returns, as a set. `expr` takes the columns
  it reads and returns one `Col` or a `std::tuple` of them, so a value moves into another column only where that
  column is written out: `sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; })`.

### Changed

- `100 * vYield` (or `vYield * 100`) stops at compile time on one sentence pointing at
  `100 * sumOf(env, rows, vYield).build()`, instead of the compiler's list of every `operator*` in scope.
- A callable that takes the same column twice - `[](FromCol a, FromCol b)` as a predicate, a term, a bound, a
  `select(expr)` or a dependent source - stops at compile time with one sentence. It used to compile and hand both
  parameters one value, which is never what such a parameter list means. `VarTable<ICol, ICol>` now says so
  directly instead of reporting a mismatched set.
- Every hash table in the set layer - `distinct`, `select`, `unite`, `intersect`, `difference`, `membership`,
  `ordered`, `groupBy` and `assertUnique` - is now the flat index `VarTable` already used: 4-byte slots of row
  numbers into rows the set already holds, instead of a node per key holding a copy of it. A `membership()` shares
  its set rather than copying the rows. These operations now stop at 2^32 - 2 rows with `std::length_error`, as
  `VarTable` and `groupBy` did.
- A `VarTable` or `ValueTable` given one row of the set it was built on - walking that set or one of its groups,
  and `sumOf(env, rows, vYield)` with the table as its term - finds it by the row's address instead of hashing it.
  Group sums with the table as the term are several times faster.
- Inside a `sumOf` term, a lookup by columns - `cost(m, i, t) * vYield(m, i, t)` - finds the row the sum is on
  without hashing it, for every table built on that row's set. The sum publishes its current row per thread while
  the term runs; a table uses it only when the row is its own and holds exactly the columns asked for, and hashes
  otherwise, so the result never depends on it.
- Variable and value tables built on one set share its row index: the first builds it, the others reuse it, and it is
  freed with the last of them. Three families on one 1e7-row set build about 40% faster, and hold one index
  instead of three.
- The tuple hash every index shares folds each column in with one folded multiply - the 128-bit product of the
  state and an odd constant, its halves xored, as Abseil's hash and foldhash do - instead of a round of splitmix64
  per column. On 1e7 rows, lookups by columns are 10-20% faster and groupings by columns that are not runs 6-17%;
  a new test checks that integer grids, aligned values, entity pointers and strings spread like a random hash.
- Set operations that hash a whole set - `distinct`, `select`, `unite`, `intersect`, `difference`, `membership` and
  `ordered` - hash a block of rows ahead and prefetch their slots too. On 1e7 rows, `unite` takes about 30% less
  time, `intersect` and `difference` about 20%, `select` about 15%.
- A `VarTable` built without a name no longer reads each row's key columns for a name it then drops: the
  variables are created with nothing but their bounds in between.
- A set's row index is built a block of rows at a time: their keys hashed and their slots prefetched first, so the
  cache misses overlap. A table on 1e7 rows builds about a quarter faster (793 ms to 582 ms for one family), and
  smaller ones gain 7-13%.
- Outside a sum, a lookup by columns tries the row the last lookup on that thread found before hashing - and, once
  lookups step from one row to the next, the row after it. `vStock.contains(i, t)` followed by `vStock(i, t)`, and a
  walk over a set looking up each row's neighbour - `vStock(i, t - 1)` - skip the hash: about seven times faster
  on a 1e7-row table. Like the sum's row, it is a hint checked against the key. A lookup in no order pays a
  comparison more: about 15 ns on a 1e7-row table, a few ns on smaller ones.
- `groupBy` tries the previous row's group before hashing the key, so grouping by the columns a set was generated
  with outermost - its leading column, typically - hashes once per group instead of once per row: about twice as
  fast. Grouping by other columns costs one comparison more per row. When every change of key opens a new group,
  the groups are the set's own runs and nothing is sorted: on 1e7 rows such a grouping takes about a third less
  time and half the page faults.

### Development

- The differential test also checks lookups by columns against each row's position, for two tables sharing a set's
  index and a third on another set: each key twice, each row's successor and predecessor, every possible key, by
  reference, and inside a sum.
- `SAN=thread tests/run_tests.sh` runs the tests under ThreadSanitizer, and CI runs it beside `SAN=1`, which now
  turns LeakSanitizer on explicitly.
- The container benchmark (`bench/`) is a designed experiment over the lot-sizing example's model build: 7 factors
  (row count 1e5 to 1e7, ownership, density, fan-out, id order, group size, families per set) in a resolution V half
  fraction of 144 cells (`--full`: 288), dreamor against hand-optimized arrays. Every phase records system time and
  page faults. The runner pins one CPU of the fastest core class unless the machine is virtualized, and records the
  machine with the results. No real solver takes part: `bench/solver/` probes CPLEX Concert and MathOpt after the
  experiment, for the containers' share of a real build and whether a gap there is theirs. Set operations - a
  projection, union, intersection, difference and a membership filter - are measured beside the model build, with
  flat writing each two ways: a `std::unordered_set`, and sorted keys merged with `<algorithm>`.

## [0.1.1] - 2026-10-22

### Fixed

- A key whose `keyName()` result has a `std::formatter` that throws no longer ends the program in
  `std::terminate` while variables are named: the name stops after the separator, with a warning.
- The logger can no longer let an exception escape from the line it writes when formatting fails.

### Changed

- A set of more than 64 columns stops at compile time on one dreamor sentence instead of internal
  errors. The limit is now documented.
- `contains`, `sliceSize`, `ord`, `isFirst` and `isLast` are `[[nodiscard]]`.

### Development

- CI tests GCC 13 and 16, Clang 18 and 23, `-O3 -DNDEBUG`, and AddressSanitizer with
  UndefinedBehaviorSanitizer, all with strict warnings as errors.
- clang-tidy, clang-format and ruff run in CI at pinned versions.
- New tests: sets of 63 and 64 columns, and a differential test against a plain-STL reference.
- The test runner compiles in parallel.

## [0.1.0] - 2026-10-20

First release.
