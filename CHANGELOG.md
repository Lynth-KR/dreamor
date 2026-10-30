# Changelog

All notable changes to dreamor. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- `dreamor::highs` (`dreamor/highs.hpp`): a backend on HiGHS's own C++ API, written the way highspy, the HiGHS team's
  Python interface, writes a model - `HighsEnv`, `VarTable`, `sumOf`, `addConstr`, `setObjective`,
  `minimize`/`maximize`, `solution`, in highspy's names. HiGHS's C++ API is a matrix API, a row its bounds and arrays;
  here a row is an expression with its bounds, a type of its own (`HighsBoundedLinearExpression`), so an inequality
  given as an objective, or a quadratic term in a row, stops at compile time. `addConstr` and `setObjective` merge
  repeated columns, in time linear in the terms, and drop the ones that cancel; an expression of a few terms keeps
  them inside itself, with no allocation. `HighsEnv` holds the model being written and the `Highs` that solves it:
  the model reaches HiGHS in one `passModel` the first time the `Highs` is reached (`env.highs()`, `minimize`,
  `solution`), and each change after that as it is written. HiGHS pays for every change it is handed: on p-median
  at 20,000 locations (2,000,000 columns, as many rows), up to the solver's start, a row a call (`addRow`) took
  1,798 ms, one `passModel` through dreamor 558, the same arrays filled by hand 276, and OR-Tools MathOpt 6,083
  (medians; `bench/scenario`). A HiGHS call that fails throws `HighsError`, with the call and its `HighsStatus`.
  `tests/run_tests.sh --highs` runs it, and CI on the HiGHS the OR-Tools prebuilt ships.
- `dreamor::exprName(family, keys...)`: a row named the way `VarTable` names variables, `"<family>_<key0>_<key1>..."`,
  each key a column by its token or a value, built in a buffer of the thread's with no allocation and no format
  string to read. `std::format` reads its format string on every call and reaches each column through a type-erased
  formatter: on 111,500 names of the lot-sizing example it took 18.0 ms where this takes 4.6 ms, the same characters.
  The pointer is valid until the thread's next `exprName`, as `FormatBuffer`'s is until its next `format`.
- `general::NameBuilder` is public, with `restart(root)` to start a new name in the same builder: the name, part by
  part, that `VarTable` and now `exprName` build.
- An entity column declared before its entity is complete is now a tested and documented use: a model can declare
  every column at the top of a file and define the entities it computes from the data further down. The entity's
  `keyName` goes right under the entity, before any name is made with the column.

- `ValueTable<Keys...>{set, value}`: data on a set, AMPL's `param {S}`. `value` is a number or a callable over the
  row's columns, as a `VarTable`'s bounds are, and the table is read like a solution's. It shares the set's row
  index with every table on the set. A model that needed a number per row - a limit per group, a capacity per
  machine and period - kept it in a map keyed by entity pointers; now it is keyed like everything else on the set.
  A `ValueTable` is a column callable itself, so it is a `VarTable`'s bound or another `ValueTable`'s value as it is;
  every backend's `VarTable` takes its bounds by `const&`, so such a bound is not copied.
- `A == B` on sets: the same rows, in any order, as `std::unordered_set`'s equality - a set holds no row twice, so
  equal sizes and every row of `A` in `B` say it. `std::ranges::equal(A, B)` still compares the order too.
- `S.ordered<A...>()`: `ord` / `next` / `prev` / `nextCyclic` / `prevCyclic` within each group of rows that agree on
  `A...`, AMPL's `next(t, S[a])` over an indexed collection of ordered sets. A walk stops (or wraps) at its group's
  ends, `ord` is the position within the group, and `first()` / `last()` / `at()`, which name positions of the whole
  set, stop on a sentence. A chain of rows within a group - a step that gives way only after the one ahead of it -
  no longer needs a `next` pointer kept in the data.

### Changed

- `FormatBuffer`'s `format()` is static, on a buffer of the thread's (one per `FormatBuffer<N, Policy>` and thread)
  instead of the object's. Naming a row changes nothing a caller can see, so a model's `const` member functions now
  name their rows, and threads name theirs at once. A buffer per object could not be made safe for that: `format()`
  returns a pointer into it, and a lock released on return would not keep another thread from overwriting the name
  before the caller copies it. A class that inherits `FormatBuffer` is no larger for it. The returned name is now
  valid until the thread's next `format()` through any `FormatBuffer<N, Policy>`, not only the same object's - the
  way it is meant to be used, handed straight to `setName`, is unchanged.

- `EntityVector`'s `insert` and `emplace` return `std::pair<const T*, bool>`, and `find` a `const T*` (`nullptr`
  when the key is absent): the element by its address, not an iterator to it. The address is the handle that
  lasts - the container's iterators are `std::deque`'s, all invalidated by the next insert - and the one a column of
  entities holds, so `modify(first, ...)` and a link from one entity to another no longer go through `&*first`.
  Code that dereferenced or compared the returned element keeps compiling; `find(key) != end()` becomes
  `contains(key)` or `!= nullptr`, and code that named the iterator type or advanced it changes too.

## [0.3.1] - 2026-10-29

### Added

- MathOpt's `sumOf` takes quadratic terms. A term that MathOpt can hold only in a `QuadraticExpression` - a
  `QuadraticTerm` (`x * x`) or a `QuadraticExpression` (`(x - 1) * (x - 1)`) - makes `build()` return a
  `QuadraticExpression` (an array of them under `split()`); a linear term still gives a `LinearExpression`. Concert's
  `IloExpr` held both already. The choice is a trait of the shared sum builder (`internal::SumTotal`), widened by the
  backend, so `where()` / `split()` work the same for both kinds.

## [0.3.0] - 2026-10-29

### Changed

- The documentation is split by what it is for. The README introduces dreamor and maps the rest;
  `docs/philosophy.md` states the principles - including ones the README only implied: a set is a set by contract,
  the solver's API stays the solver's, misuse stops at compile time in one sentence, the standard library is the
  precedent, speed is measured - with the naming, the rules of use and what dreamor leaves out; `docs/guide.md`
  explains every part in use; `docs/reference.md` lists the API. Each in English and Korean (`docs/ko/`).
- `FlatIndex` has one probe for tables with tags and without, not one each. With two, `find()` grew too large for
  its callers to inline it: `Ordered::prev(t)` - 3 ns a call - took 1.4 times as long since 0.2.0's tags, under GCC 13
  and Clang 18 alike. The catalog's families now total 0.93-0.99 of 0.2.0's time on both compilers (order, setop
  and lookup gain the most), and Clang's index builds 0.79-0.87. Against the library before the tags, a `prev(t)`
  still costs about 0.2 ns more - small tables check a tag of 0 too - while lot sizing through CPLEX builds in
  0.96-0.98 of that library's time on both compilers.
- The README introduces the two headers model code can use beside the library: `general/format_buffer.hpp`
  (`FormatBuffer`, names built in a fixed buffer) and `logger/logger_light.hpp` (`DREAMOR_LOG_*`, `FileSink`); the
  reference lists what each offers. The guide states the order a model in a struct needs - its sets before the
  `VarTable`s that take them, as earlier members or a base class - instead of prescribing the base class. The
  README's quick start is one model class that shows both: its sets declared before its `VarTable`s, and
  `FormatBuffer` naming its rows.

- `TupleHash` takes an integer, an enum or a pointer as its bits (`general::hashElement`), not through `std::hash`,
  which each standard library defines its own way: the MSVC STL runs FNV-1a over a pointer's eight bytes before
  `TupleHash` mixes it again. Under libstdc++ the hashes are those `std::hash` gave; under the MSVC STL a key's
  lookup no longer carries FNV-1a, so it compiles to the code it does on Linux. `std::hash<Col<...>>` hashes its
  value the same way. Measured with the catalog, the library before against after: no performance regression on
  either platform. Under Clang 18 the machine code is the same, under GCC 13 no family's total moves (replicated
  under another seed), and on Windows (clang-cl 19) no family is slower while the ones that hash run in 0.63-0.71 of
  the time (order, lookup, setop) and 0.89-0.97 (group, slice, make, data).

### Added

- `CONTRIBUTING.md` (and its Korean version): how to report a bug, propose a feature, build and test, and get a
  change in. Commits in a pull request are signed off under the Developer Certificate of Origin (`git commit -s`).
- `CODE_OF_CONDUCT.md`: the Contributor Covenant 2.1.
- Issue forms for a bug report and a feature request (the latter asks that the philosophy was read), and a pull
  request checklist. Questions go to GitHub Discussions; issues, pull requests and discussions are in English.
- `NOTICE`: the copyright, and the attribution of the OR-Tools samples ported in `examples/mathopt_samples/`.
- The guide's "Steering the visit order": `makeSet` plans from columns, not sizes, so a user who knows which
  predicate is the selective one builds that part first as a set of its own and hands it on - the same rows and
  type, the predicate run over the smaller product. No option names an order.
- `SECURITY.md`: vulnerabilities are reported privately, through GitHub's private vulnerability reporting, and a
  broken precondition - a repeat handed to `makeSet` - is not one.
- A column prints as its name token under `std::format` (`std::formatter<Col<...>>`): the token `VarTable` writes for
  it in a variable name - its `keyName()`, else its entity's `primaryKey()`, else its value. A row is then named the
  way its variables are, as the README's quick start now names its capacity rows:
  `model.add(totalLoad <= machine->capacity).setName(format("Capacity_{}_{}", machine, period))`, with `format`
  from `FormatBuffer`. A column with no token has no formatter, and a column takes no format spec.
- Windows, through clang-cl and the MSVC STL: the library and the stub tests build there under the tests' strict
  warnings and pass, and the CPLEX tests solve against CPLEX 2212's Windows libraries. Checked by hand, not on CI.
  MSVC's own compiler (cl.exe) is not supported, and will not be. The platforms are Linux and Windows; where Linux
  runs - on the machine, under WSL2, in a Docker or Podman container - is the user's to choose. The column formatter
  no longer takes `std::string_view`'s iterator for a pointer, and the logger reads `NO_COLOR` and opens a
  `FileSink`'s file through the Windows CRT's own functions (`_dupenv_s`, `_fsopen` - a file another program can still
  read), so a strict Windows build reports no deprecated `getenv` or `fopen`.

### Development

- Every CI job stops after 20 minutes. Each takes under 5; one past 20 is stuck - a package mirror that stopped
  answering held a job for 21 minutes - and would otherwise hold a runner for GitHub's default of six hours.
- The benchmark runners count instructions wherever the machine lets a process count its own (`--pmu auto`, the
  default; `none` turns it off), and `bench/catalog/compare.py` puts each counter's ratio beside the time's. Time
  that the instructions do not follow is memory; time they follow is the code the compiler made.
- How a performance change is judged (`AGENTS.md`, and in `CONTRIBUTING.md` for people): every A/B is built with
  both compilers at CI's floor, GCC 13 and Clang 18. A phase's loss is weighed against the gain and stated per
  operation, not held to a fixed bound; what may not get slower is the whole - a catalog family's total, a scenario
  through the real solver. A loss the allocator's history makes, shown by its page faults, is not the change's cost.
- CI checks out with `actions/checkout@v7`, which runs on Node.js 24: v4's Node.js 20 is deprecated on GitHub's
  runners.
- Every `if`, `else` and loop has braces, and a short `if` stays on one line, braced (`.clang-format`'s
  `InsertBraces`); clang-tidy's `readability-braces-around-statements` is on again. `.git-blame-ignore-revs` lists the
  reformatting commit, so `git blame` passes over it (`git config blame.ignoreRevsFile .git-blame-ignore-revs`; GitHub
  reads the file on its own).
- `tools/format.sh` and `tools/tidy.sh` run the pinned clang-format, clang-tidy and ruff through `uvx`: uv is the one
  tool to install, nothing goes on PATH, and the pins live in the scripts and `ruff.toml` alone - CI installs uv
  instead of the three tools. `tools/clang-format` is the pinned clang-format as one executable, for an editor's
  format-on-save.
- The repository's VS Code settings format C++ on save with that pinned clang-format (the clang-format extension,
  `clang-format.executable`), not with clangd's built-in formatter, whose version is whatever clangd the editor runs.
  The pins move with CI's clang-latest job: at each LLVM major, clang-format and clang-tidy go to that major's newest
  patch.
- `tools/hooks/pre-push` no longer runs the local CI: `tools/ci_local.sh` records a pass by the tree it tested, and
  the hook stops a push to main whose tree has none. A push that changes only Markdown needs none, as CI skips it.
- The contract-failure tests keep the kernel from dumping their core (`PR_SET_DUMPABLE`): each case stops on purpose,
  and a desktop session's crash reporter announced every one as a crash.
- The benchmarks' results are the reference run: bare metal, the CPU's frequency fixed, the runner pinned to one
  core, resolution VI with 5 repetitions, built with GCC 13 and with Clang 18. Against the fastest hand-written arm
  dreamor is ahead in every catalog family's total on both compilers, and through a solver it builds what
  hand-optimized code builds within 5%.

## [0.2.0] - 2026-10-25

### Added

- `dreamor::EntityVector<T>`: a container that holds each entity once, by the primary key the entity declares
  with `primaryKey(const T&)` next to its type (found by ADL, as `keyName` is; a composite key is a `std::tuple`). It
  reads like `std::vector` and inserts like `std::set`: `insert` and `emplace` return `{the element with that key,
  whether it went in}`. Elements never move (a `std::deque` underneath) and none is erased, so pointers into the
  model stay the entity's; access is const, and `modify(entity, fn)` is the one way to change an element, its key
  unchanged. A debug build checks both of `modify`'s preconditions. As a set source it is any container; a debug
  build skips checking it for repeats.
- A pointer column whose entity has no `keyName()` but a `primaryKey()` names its variables by that key; a debug
  build says so once per column.
- A debug build (no `NDEBUG`) checks the set contract at `makeSet`: a container or a spread range repeating an
  element, or a dependent source returning one twice, stops the program on one line naming the source. A
  dependent source's range is checked each time it is returned, or once if it is borrowed and contiguous - a span
  or a reference into the model - since the same address and size is then the same range.
- `makeSet` joins set sources that share a column, as AMPL's `{(m,i) in ROUTES, (i,t) in OPEN}` does:
  `makeSet<>(sROUTES, sOPEN)`. The set visited second is looked up by the shared columns rather than scanned, so a
  join costs the two sets plus the result instead of their product.
- `S.select(expr)`, the general `setof`: each row becomes what `expr` returns, as a set. `expr` takes the columns
  it reads and returns one `Col` or a `std::tuple` of them, so a value moves into another column only where that
  column is written out: `sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; })`.

### Changed

- `makeSet`'s planner breaks a tie between a dependent source and a container in favour of the dependent source,
  before falling back on declaration order: the list a dependent source hands back per binding - a machine's
  items - is usually far shorter than a whole container, as statistics-free join ordering puts one-to-many joins
  before many-to-many ones. Across made-up sets of 3 to 16 sources with dependent lists shorter than containers,
  the median order now costs 3.05 times the best instead of 3.61 (p90 149 instead of 216); with lists as long as
  containers it is unchanged within noise. Sets where it applies come out in another row order, which was never
  fixed. The planner (`planVisitOrder`) is `constexpr` rather than `consteval`, so that the benchmark can call it.
- A set holds no row twice, by contract: no source handed to `makeSet` - a container, the range a dependent source
  returns, a set - holds an element twice, and an entity is one object in one container. Breaking it is undefined
  behavior, as with the standard library's preconditions. Every operation keeps a set free of repeats on its own,
  so a `VarTable` is one variable per key by construction: its constructor no longer throws
  `std::invalid_argument` on a repeated row, and a debug build stops on one instead.
- `100 * vYield` (or `vYield * 100`) stops at compile time on one sentence pointing at
  `100 * sumOf(env, rows, vYield).build()`, instead of the compiler's list of every `operator*` in scope.
- A callable that takes the same column twice - `[](FromCol a, FromCol b)` as a predicate, a term, a bound, a
  `select(expr)` or a dependent source - stops at compile time with one sentence. It used to compile and hand both
  parameters one value, which is never what such a parameter list means. `VarTable<ICol, ICol>` now says so
  directly instead of reporting a mismatched set.
- Every hash table in the set layer - `select`, `unite`, `intersect`, `difference`, `membership`, `ordered` and
  `groupBy` - is now the flat index `VarTable` already used: 4-byte slots of row
  numbers into rows the set already holds, instead of a node per key holding a copy of it. A `membership()` shares
  its set rather than copying the rows. These operations now stop at 2^32 - 2 rows with `std::length_error`, as
  `VarTable` and `groupBy` did. From 2^16 slots on, a slot's spare high bits carry the top of its key's hash, so a
  probe reads a key only where they agree: on 1e6 rows, misses take about 0.57 of the time, lookups half of which
  miss 0.82, building a row index 0.74.
- A `VarTable` or `ValueTable` given one row of the set it was built on - walking that set or one of its groups,
  and `sumOf(env, rows, vYield)` with the table as its term - finds it by the row's address instead of hashing it.
  Group sums with the table as the term are several times faster.
- Inside a `sumOf` term, a lookup by columns - `cost(m, i, t) * vYield(m, i, t)` - finds the row the sum is on
  without hashing it, for every table built on that row's set. The sum publishes its current row per thread while
  the term runs; a table uses it only when the row is its own and holds exactly the columns asked for, and hashes
  otherwise, so the result never depends on it.
- Variable and value tables built on one set share its row index: the first builds it, the others reuse it, and it is
  freed with the last of them. Three families on one 1e7-row set build about 40% faster, and hold one index
  instead of three. The index hashes the rows on the first lookup that needs it, not when the table is built: a
  table reached only by row - walking its set or a group, or as a sum's term - never pays for it, which is about
  a third of building the first table on a set (stub solver, 1e6 - 1e7 rows).
- The tuple hash every index shares folds each column in with one folded multiply - the 128-bit product of the
  state and an odd constant, its halves xored, as Abseil's hash and foldhash do - instead of a round of splitmix64
  per column. On 1e7 rows, lookups by columns are 10-20% faster and groupings by columns that are not runs 6-17%;
  a new test checks that integer grids, aligned values, entity pointers and strings spread like a random hash.
- Set operations that hash a whole set - `select`, `unite`, `intersect`, `difference`, `membership` and `ordered` -
  hash a block of rows ahead and prefetch their slots too. On 1e7 rows, `unite` takes about 30% less
  time, `intersect` and `difference` about 20%, `select` about 15%.
- A `VarTable` built without a name no longer reads each row's key columns for a name it then drops: the
  variables are created with nothing but their bounds in between.
- A set's row index is built a block of rows at a time: their keys hashed and their slots prefetched first, so the
  cache misses overlap. A table on 1e7 rows builds about a quarter faster (793 ms to 582 ms for one family), and
  smaller ones gain 7-13%.
- Outside a sum, a lookup by columns tries the row the last lookup on that table found before hashing - and, once
  lookups step from one row to the next, the row after it. `vStock.contains(i, t)` followed by `vStock(i, t)`, and a
  walk over a set looking up each row's neighbour - `vStock(i, t - 1)` - skip the hash: about seven times faster
  on a 1e7-row table. Like the sum's row, it is a hint checked against the key. A thread keeps one for each of the
  last four tables it looked up, so code that takes turns between tables - `x(l, c) <= y(l)` a row - keeps both
  hints: p-median's link constraints build in about a third of the time they took with one hint for all tables.
  Before a table's hash is built, a lookup with no hint tries the set's first row, so a walk over the whole set
  never builds the hash at all. A lookup in no order pays a comparison more: about 15 ns on a 1e7-row table, a few
  ns on smaller ones.
- `intersect` and `difference` keep the rows of the left set that pass, as they come, without an index of the rows
  already kept: a set holds no row twice. `unite` is the left set, then the right set's rows the left lacks, so only
  the left is indexed. On 1e6 rows, `intersect` takes 0.61-0.80 of the time, `difference` 0.41-0.81, `unite` about
  0.73.
- `collect()` reserves its rows when their number is known before it makes them - every source a sized range of
  its own, no dependent column, no join, no predicate - instead of growing the vector: 5e6 rows of a product take a
  third of the time.
- Variable names: a text token - a `keyName()` returning a `std::string` - is copied into the name instead of going
  through `std::format`, and the name builder is inlined into the loop that creates the variables. The names are
  the same bytes. Three named families of 1e6 rows with text keys build in about two thirds of the time; with
  integer keys, 4-6% faster. g++ takes about 1 s more to compile a 63- or 64-column table (README, compile cost).
- `groupBy` tries the previous row's group before hashing the key, so grouping by the columns a set was generated
  with outermost - its leading column, typically - hashes once per group instead of once per row: about twice as
  fast. Grouping by other columns costs one comparison more per row. When every change of key opens a new group,
  the groups are the set's own runs and nothing is sorted: on 1e7 rows such a grouping takes about a third less
  time and half the page faults.

### Removed

- `concat`, `S.distinct()` and `S.assertUnique()`. A set never holds a row twice, so there is no repeat for them to
  keep, drop or report.

### Development

- `tests/contract_fail/`: programs that break a contract on purpose. `tests/run_tests.sh` runs each in a debug build
  and requires it to stop with the message its `// expect:` line names.
- The differential test also checks lookups by columns against each row's position, for two tables sharing a set's
  index and a third on another set: each key twice, each row's successor and predecessor, every possible key, by
  reference, and inside a sum.
- `SAN=thread tests/run_tests.sh` runs the tests under ThreadSanitizer, and CI runs it beside `SAN=1`, which now
  turns LeakSanitizer on explicitly.
- CI runs on pushes to main and on pull requests, or by hand for a work branch, and not on a change of Markdown alone;
  the builds wait for lint (format, then tidy), and a newer push cancels the run still going on the same ref. Format
  runs inside the lint job and the package check inside the GCC 13 stub job, and GCC 13's plain `-O1 -g` build is
  left to `SAN=1`, which is that build instrumented: ten jobs instead of thirteen.
- `tests/run_tests.sh` parses each compile_fail folder's prelude once, into a precompiled header, which cuts the cases'
  time by about 2.5x with the same errors and notes; `COMPILE_CHECKS=0` skips the compile_fail cases and the syntax
  checks, which neither OPT nor SAN changes, and CI runs them once per compiler. `SAN=thread` skips the contract_fail
  cases.
- `tools/ci_local.sh` runs CI's jobs locally on a commit checked out apart, once every tool CI has is present, and
  `tools/hooks/pre-push` runs it on what is pushed to main (`git config core.hooksPath tools/hooks`).
- The benchmarks are two layers of designed experiments. `bench/catalog/` measures each operation - lookups,
  grouping, set operations, slices, generation, order, entities and solution values - against hand-written arms
  (hash maps with an identity or a mixed hash, direct arrays, sorting, `std::map`) over the shapes data takes and
  the ways models reach it: a resolution IV screening design per family over k columns, N and six two-level data
  factors, against a lean Concert stub, and `--builds` to compare two builds of the library run by run.
  `bench/scenario/` builds whole models end to end, by dreamor and by hand, up to a solve with no time - the
  modeling-language benchmarks' measure - on the stub, CPLEX and MathOpt: p-median at those benchmarks' sizes, and
  the lot-sizing example as written, against hand-optimized code and against the example's own `std::map`
  version. Every run is a fresh process under a memory layout of its repetition's own (an environment padding and a
  preconditioned heap), records system time, page faults and, with `--pmu`, hardware counters per phase, and
  checks that every arm did the same work. The container benchmark of the lot-sizing build (`bench/run_doe.py`) is
  retired: the catalog covers its operations and factors, the lot-sizing scenario the example's build.
  `bench/solver/` probes how Concert's variable creation meets the caller's code.
- `bench/plan/model.cpp` calls the planner on thousands of made-up sets of 3 to 16 sources - six predicate shapes,
  with and without dependent sources, sizes and selectivities spread over two orders of magnitude - and sets its
  order against the best, the worst and declaration order by the work the loops do, with the best and worst exact
  from a dynamic program over subsets. It runs in a minute, and checks its cost model against real loops.
- `bench/plan/` times `makeSet` in every order its sources can be visited in, forced through dependent sources,
  against the order `plan.hpp` picks: four predicate shapes and a join, sizes and selectivities varied, with
  predicates a branch predictor can or cannot learn.

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
