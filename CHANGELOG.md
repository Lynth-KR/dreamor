# Changelog

All notable changes to dreamor. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- `S.select(expr)`, the general `setof`: each row becomes what `expr` returns, as a set. `expr` takes the columns
  it reads and returns one `Col` or a `std::tuple` of them, so a value moves into another column only where that
  column is written out: `sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; })`.

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
