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
  otherwise, so the result never depends on it. Elsewhere a lookup by columns hashes as before.

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
