# AGENTS.md

Instructions for coding agents working **on** this repository. For writing models **with** dreamor, see the skill in
`skills/dreamor/` (or `README.md` and `docs/`). For people contributing, `CONTRIBUTING.md` says the same rules in
brief.

dreamor is a header-only C++20 modeling layer: index sets (solver-independent), variable families (`VarTable`), and
sums (`sumOf`), with two backends, CPLEX Concert (`dreamor::concert`) and OR-Tools MathOpt (`dreamor::mathopt`).
Version 0.2.0, Apache 2.0.

## Layout

| Path | What |
|---|---|
| `include/dreamor/index_set.hpp`, `include/dreamor/index_set/` | Set layer: `Col`, `EntityVector`, `makeSet`, `IndexSet`, `Grouped`, `Ordered`, set algebra. Must not include any solver header |
| `include/dreamor/value_table.hpp` | `dreamor::ValueTable`, shared by both backends |
| `include/dreamor/concert.hpp` | Concert backend |
| `include/dreamor/mathopt.hpp` | MathOpt backend |
| `include/dreamor/internal/` | Shared backend machinery (`var_table_core.hpp`, `row_index.hpp`, `sum_builder.hpp`) |
| `include/dreamor/general/`, `include/dreamor/logger/` | Utilities (`tuple_hash.hpp`, `flat_index.hpp`, `format_buffer.hpp`, `third_party_headers.hpp`, `logger_light.hpp`) |
| `examples/` | Lot-sizing example: `production_planning_sets.hpp` (solver-free), `production_planning.hpp` (Concert), `production_planning_mathopt.hpp` (MathOpt), `production_planning_plain.hpp` (without dreamor, for comparison). `mathopt_samples/`: three OR-Tools MathOpt samples ported to dreamor, with a walkthrough of each port (keep Google's license header and the modification notice on these files) |
| `tests/` | Framework-free tests, `tests/run_tests.sh`, Concert stub (`tests/stub/`), compile-fail cases |
| `bench/` | Benchmarks, not run in CI (`bench/README.md`). `bench/catalog/`: layer 1, each operation against hand-written code over data shapes and access patterns, a resolution IV screening design per family (lookups, grouping, set operations, slices, generation, order) and a full design of its own for entities and solution values, against the lean Concert stub. `bench/scenario/`: layer 2, whole models (p-median, the modeling-language benchmarks' model, and the lot-sizing example as written) built by dreamor and by hand up to the solver's start, on the stub, CPLEX and MathOpt. `bench/common/`: the measurement header, the run's JSON result, the lean stub, the machine helpers. `bench/solver/`: how Concert's variable creation meets the caller's code. `bench/plan/`: how `makeSet`'s visit order compares with every other order - a cost model over made-up sets of 3 to 16 sources, and a timed three-source experiment |
| `skills/dreamor/` | Agent Skill for users writing dreamor code |
| `README.md`, `docs/` | User documentation, English and Korean (`README.ko.md`, `docs/ko/`): the README introduces and maps; `docs/philosophy.md` the principles and rules of use, `docs/guide.md` every part in use, `docs/reference.md` the API in tables |
| `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `NOTICE` | For people contributing (English and Korean), the Contributor Covenant 2.1, how to report a vulnerability privately, the copyright and attribution notice |
| `cmake/`, `CMakeLists.txt` | `dreamor::dreamor` INTERFACE target and install/package config |

## Build and test

There is nothing to build; the tests do not go through CMake.

```bash
tests/run_tests.sh                   # default; needs nothing but a C++20 compiler
tests/run_tests.sh --cplex [DIR]     # real CPLEX Studio ($CPLEX_STUDIO_DIR, default /opt/ibm/ILOG/CPLEX_Studio2212)
tests/run_tests.sh --mathopt [DIR]   # OR-Tools C++ prebuilt ($ORTOOLS_DIR, default /opt/ortools), solves with HiGHS
CXX=clang++-18 tests/run_tests.sh    # compiler choice; default g++
OPT="-O3 -DNDEBUG" tests/run_tests.sh # optimization; default -O1 -g
SAN=1 tests/run_tests.sh             # Address + UndefinedBehavior + Leak sanitizers (stub mode only)
SAN=thread tests/run_tests.sh        # ThreadSanitizer, a build of its own (stub mode only)
JOBS=4 tests/run_tests.sh            # parallel compiles; default every core (tests still report in order)
COMPILE_CHECKS=0 tests/run_tests.sh  # skip compile_fail and the syntax checks: they depend on the compiler alone
tools/format.sh                      # clang-format (C++) and ruff (Python) in place; --check changes nothing
tools/tidy.sh                        # clang-tidy; needs compile_commands.json (tools/gen_compile_commands.py)
tools/ci_local.sh [REV]              # what CI runs, on one commit checked out apart; needs every tool CI has
```

- **Default mode** runs every `tests/test_*.cpp` against the Concert stub, checks every `tests/compile_fail/*.cpp`,
  runs every `tests/contract_fail/*.cpp` (not under `-DNDEBUG` or `SAN=thread`), and syntax-checks
  `tests/cplex/*.cpp` and the Concert example. Two of those are stress tests:
  - `test_arity.cpp` takes sets of 63 and 64 columns through everything that works on whole rows, and
    `compile_fail/makeset_too_many_columns*.cpp` pin the 65th column's message. The three files are generated by
    `tools/gen_arity_tests.py` - edit the script and re-run it, never the files.
  - `test_differential.cpp` checks the set layer and sumOf against a plain-STL reference over a few hundred random
    cases from a fixed seed. A change to the semantics of rows (order, repeats, grouping, neighbours) has to change
    its reference too.
- **`--mathopt`** runs `tests/mathopt/*.cpp`, the MathOpt example, the ported samples in
  `examples/mathopt_samples/`, and `tests/mathopt/compile_fail/`.
  `test_cross_backend.cpp` builds the example on both backends and compares the constraints.
- **`--cplex`** solves with real CPLEX. CPLEX is not on CI, so run it locally when you touch Concert-facing code
  and have CPLEX available.
- **Warnings** are what dreamor's own code is held to; the CMake target adds none, so users keep their own flags.
  The library, the tests, and the examples build with `STRICT` in `tests/run_tests.sh` (`-Wconversion`,
  `-Wsign-conversion`, `-Wold-style-cast`, `-Wfloat-equal`, ... and `-Werror`): a header that warns there would warn
  in a user's strict build. The ported samples and the compile_fail cases build with `-Wall -Wextra` only - a
  warning must not become a compile_fail case's first error. Tests compare doubles with `exactlyEqual` /
  `isExactlyZero` from `tests/float_compare.hpp`, never with `==`.
- **CI** (`.github/workflows/ci.yml`) runs a lint job (`tools/format.sh --check`, then `tools/tidy.sh`), and once it
  passes: the stub job on Clang 18 at `-O1 -g` and `-O3 -DNDEBUG` and on GCC 13 at `-O3 -DNDEBUG`, a sanitize job
  (`SAN=1` and `SAN=thread`, GCC 13; `SAN=1` is GCC 13's `-O1 -g` build), gcc-latest and clang-latest (stub mode on
  the newest releases), and the MathOpt job on both compilers. The compile_fail cases and the syntax checks run once
  per compiler; the other jobs set `COMPILE_CHECKS=0`. The GCC 13 stub job and the MathOpt job also install the CMake
  package and build `tests/cmake_consumer` (`tests/cmake_consumer_mathopt`) through `find_package(dreamor)`. CI runs
  on pushes to main and on pull requests, not on a change of Markdown alone; a work branch runs it by hand
  (`gh workflow run tests --ref <branch>`).
- **Local CI** (`tools/ci_local.sh`) runs the same jobs on this machine, over a commit checked out in a temporary
  worktree, so it tests what a push publishes and not the working tree. It checks for every tool CI installs first -
  GCC 13, Clang 18 and 23, podman with the `gcc:16` image, cmake, OR-Tools at `$ORTOOLS_DIR`, and uv (for the
  pinned formatters and clang-tidy) - and runs nothing if one is missing. `tools/hooks/pre-push` runs it on every
  commit pushed to main; turn it on per clone with `git config core.hooksPath tools/hooks`, skip it once with
  `--no-verify`.
- **Compiler support** is a range tested at both ends, nothing in between: the floor is GCC 13 / Clang 18 (raising
  it is a decision, not a cleanup), the ceiling the newest releases (GCC 16 from the `gcc:16` image, Clang 23 from
  apt.llvm.org). Bump the ceiling at each major release - GCC every spring, LLVM every March and September - in
  gcc-latest and clang-latest. A new warning or a changed first error there is what users of that compiler will see:
  fix it, then bump. Locally: `CXX=clang++-23 tests/run_tests.sh`, and
  `podman run --rm -v "$PWD":/src -w /src docker.io/library/gcc:16 sh -c 'CXX=g++ tests/run_tests.sh'`.
- **Formatting** is `.clang-format` (clang-format 23.1.3) and `ruff.toml` (ruff 0.17.1), pinned because output differs
  between versions. The scripts run the pinned versions through `uvx`, so all they need is
  [uv](https://docs.astral.sh/uv/); nothing is installed on PATH, and another project's pins do not collide.
  `examples/mathopt_samples/` is left as ported, and the code blocks of the README and `docs/` are formatted by
  hand. The script also adds the SPDX license line where it is missing.
  CI runs `tools/format.sh --check`. To bump a formatter, change its pin (`CLANG_FORMAT_VERSION` in `tools/format.sh`,
  `required-version` in `ruff.toml`), run `tools/format.sh`, commit only the reformatting, and add that commit to
  `.git-blame-ignore-revs`. Time to bump: when clangd in the editor gets ahead of the pin and format-on-save starts
  to disagree with CI. `tools/clang-format` is the pinned clang-format as one executable, for an editor's
  format-on-save (VS Code: `clang-format.executable`).
- **Linting** is `.clang-tidy` (clang-tidy 23.1.3, run through `uvx` as well): broad check families, with
  the few that clash with this codebase turned off and the reason written beside each. tests/ and examples/ relax
  what test and example code does on purpose; tests/stub, the compile_fail cases, the ported samples and bench/ are
  not checked. An exception to a check that stays on is `NOLINT(check): reason` - never a bare NOLINT. Review what
  `tools/tidy.sh --fix` writes: applied from several translation units at once, a fix can land twice or garble a
  call, and const goes on the right.
- **Done means** default mode passes on both compilers, `--mathopt` passes if you touched anything the MathOpt
  backend or the shared machinery uses, `tools/format.sh --check` passes, and `tools/tidy.sh` reports nothing.

## Design rules

Changes must keep these. They are the library's reason to exist, not style preferences.

- **Columns are named, not positional.** Callables take the column types they read; lookups take key columns in
  declaration order or one row. Bare values and braced lists stay rejected.
- **Sets are immutable.** New sets come from derivation, never mutation.
- **Sets are sets, by contract.** No source handed to `makeSet` holds an element twice, and an entity is one object
  in one container: a precondition, undefined behavior if broken, as in the standard library. dreamor's own
  operations never make a repeat, so a set never holds a row twice and a `VarTable`'s key-to-variable map is
  one-to-one. No user-facing check or deduplication (`distinct`, `assertUnique`); a debug build checks every source
  in `makeSet` (Diagnostics).
- **One VarTable per variable family.** Variables are created only in the `VarTable` constructor, and a table's
  structure never changes after construction.
- **No new user-facing concept for performance.** Internal hashing and set sharing are fine; new knobs in the API
  are not.
- **Optimize for a mechanism, not for the benchmark.** The benchmark's arms, factors, and data are this repository's
  own, so a win there can be a fit to them. An optimization stands on a property dreamor guarantees (a contract, the
  structure of generation), not on how the benchmark draws its data; its effect holds across the experiment's cells,
  not in a few; it serves an operation real models perform; and it lowers a cost rather than moving it to another
  phase. Check it against the whole experiment and on data it was not tuned on (another seed or machine), and build
  every A/B with both compilers at CI's floor, GCC 13 and Clang 18: they inline and lay out code differently, and a
  change can hold under one alone (newer compilers are welcome, not required). What a loss elsewhere may cost depends
  on how far the library has come:
  - A fundamental change (a new data structure, a first optimization of an operation) leaves no other phase or cell
    worse.
  - Squeezing a library that is already tuned works on a Pareto frontier, where a gain somewhere usually costs a
    little elsewhere. A loss is accepted when all four of these hold, and the commit message states it:
    - **Asymmetric:** the gain is at least 5 times the loss, 10 times preferred (15-50% faster against 2-4% slower).
    - **Where it matters:** the loss falls where building is already fast or the shape is uncommon (small N, data in
      cache), the gain where models wait (large N, past the cache) - as a service weighs its P95/P99 latency above
      its average.
    - **The whole holds, absolutely:** a phase's loss is weighed, not capped - against the gain (above), and stated
      as what one operation costs (ns a call, a row), measured with the phase alone (`run.py --phases`), not after
      what other phases left in the heap. What is absolute is the whole: no family's total in the catalog and no
      scenario - a whole model, through the real solver - is slower at any N, by its 95% interval. The catalog's
      mix of phases is the benchmark's own; a scenario is how a model actually weighs them.
    - **Net positive:** over every cell of the experiment, the geometric mean ratio is a gain whose 95% interval
      excludes no change.

    A phase's loss that the allocator's history makes, not the changed code, is not the change's cost: the phase
    runs the same instructions and pays for memory an earlier phase used to leave it - blocks freed by vector
    doublings the change no longer makes, or the glibc mmap threshold those frees raised. Show the mechanism (the
    phase's page faults; the loss gone or shrunk with `GLIBC_TUNABLES=glibc.malloc.mmap_threshold=33554432`) and
    state it in the commit message with its numbers. Instructions tell the two apart: `run.py` counts them where the
    machine can (`--pmu auto`), and `compare.py` puts their ratio beside the time's - a loss they follow is the code
    the compiler made, one they do not is memory.
- **One way to do one thing.** No second syntax: no pipe operators, no free-function aliases of members. Two
  combinations of tools reaching the same result is not a second way: never add a rule that forbids a combination
  because another one gives the same result.
- **Reach for the standard library's precedent.** When a design decision has an analogue in the C++ standard
  library or its implementations, propose that first and name it: `std::set::emplace`'s `pair<iterator, bool>`
  for an insert that may not insert, libstdc++'s assertions for checks that only debug builds pay for,
  `std::ranges::borrowed_range` for a range whose elements outlive the call that returned it.
- **The solver's own API stays the solver's.** Constraints, objectives, solving, duals, bounds after construction,
  and names are not wrapped. Backends take each solver's notation (`IloEnv`/`ILOFLOAT` vs `Model&`/`is_integer`).
- **Out of scope.** Do not add any of these without the maintainer asking:
  - constraint tables;
  - keyed dual or reduced-cost accessors;
  - column generation;
  - coefficient-array overloads;
  - user-facing duplicate checks or deduplication of sets (see "Sets are sets, by contract");
  - constraint programming (interval variables, `noOverlap`, cumulative functions).

## Backend parity

These behave identically on both backends. A change to one is a change to both:

- bounds as numbers or column callables;
- variable names `<name>_<key0>_<key1>...`;
- `std::out_of_range` for a lookup outside the set;
- iteration in set order;
- `sumOf`'s `where()` / `split()` / `build()`;
- a `VarTable` as a sum term.

Backend-only features stay in their backend:
- Concert only: semi-continuous variables, quadratic terms.
- MathOpt only: `std::logic_error` from `solution()` when there is no feasible solution, because MathOpt would
  otherwise CHECK-fail.

Shared logic belongs in `include/dreamor/internal/`.

## Benchmark

`bench/` is not part of the test suite or CI. When a change touches what the benchmarks exercise
(`makeSet`, `groupBy`, `VarTable` construction or lookup, `sumOf`, the set algebra, `Ordered`,
`EntityVector`, `ValueTable`), check with `bench/catalog/build.sh` and `bench/scenario/build.sh`:
- both still build;
- every arm still does the same work: a quick run of each family the change reaches
  (`python3 run.py --family <family> --n-levels 100000 --reps 1` in `bench/catalog/`, then
  `report.py`) and of a scenario (`python3 run.py --scenario lotsizing --levels 170 --reps 1` in
  `bench/scenario/`, then `report.py`) report no check mismatch.

A timing claim compares builds: the catalog's `run.py --builds base=<old>,head=<new>` and `compare.py`
over the families the change reaches, the whole experiment and not a few cells (see "Optimize for a
mechanism"), and the scenarios for a change a whole model would feel. `python3 -m unittest
common.test_machine` (in `bench/`) tests the CPU choice. An agent's sandbox is not a benchmark
machine.

## Diagnostics

Misuse is stopped at compile time with a `static_assert` whose message is one sentence saying what to write
instead (e.g. `sumOf(...) is not an expression yet: add .build() - ...`). When adding or changing a diagnostic:

1. Add a case to `tests/compile_fail/` (Concert/set layer) or `tests/mathopt/compile_fail/`. Each case is one file
   that starts with the SPDX line, then a `// expect: <text>` line, and includes `prelude.hpp`.
2. The runner requires `<text>` in the compiler's **first** error and its notes, not just a rejection. This must
   hold on both GCC 13 and Clang 18. Arrange overloads so the dreamor message comes first, not a wall of template
   errors.
3. `control.cc` in each folder must keep compiling; it proves that a rejection comes from the library.
4. Update the message table in `skills/dreamor/references/diagnostics.md`.

A broken contract - a precondition such as "no source holds an element twice" - is undefined behavior, and only a
debug build checks it: `DREAMOR_DEBUG_FATAL` prints one sentence naming the contract and aborts, as libstdc++'s
assertions do. Each check has a case in `tests/contract_fail/`: a program that breaks the contract, with the same
`// expect: <text>` line, which the runner looks for in what the program prints before it stops.

## Concert stub

`tests/stub/` stands in for the Concert headers so CI runs without CPLEX. It mirrors the real header layout. For
example, `IloModel` has its own header, as in real Concert, so a missing include fails here as it would against
CPLEX. If library code starts using another Concert API, add it to the stub the way the real headers declare it.
Confirm with `--cplex` when possible.

## Code style

Every code file starts with `// SPDX-License-Identifier: Apache-2.0` (`#` in Python, shell, and CMake; after the
`#!` line in scripts); `tools/format.sh` adds it and CI checks it. Each header then has this convention block.
Follow it in library code:

- types `PascalCase`;
- variables `snake_case`, members with a trailing underscore (`rows_`);
- functions `camelCase`;
- constants `kPascalCase`;
- macros `SCREAMING_SNAKE_CASE`, prefixed `DREAMOR_` (a bare `CHECK` collides with Abseil and glog, see
  `tests/check.hpp`).

User-facing examples (README, `docs/`, header comments, examples, skill) follow the user conventions instead:
- columns `PascalCaseCol`, sets `sSCREAMING_SNAKE`, variable families `vPascalCase`;
- callables taking columns in `snake_case`;
- everything else in `camelCase`.

Primitive types follow the domain, everywhere (library, tests, examples, bench):
- a count, size, or index is never signed: `std::size_t` by default, a fixed-width `std::uint32_t` (or narrower)
  where storage size matters;
- signed only where a negative value means something (negative stock, a step backwards: `std::ptrdiff_t`);
- prefer types whose size is explicit (`std::size_t`, `std::uint32_t`, `std::int16_t`, ...) over `int`, `long`,
  `unsigned`. The exceptions are what the language or a mirrored API fixes: `main(int argc, ...)`,
  `operator++(int)`, the process exit status, `IloInt` in the Concert stubs.

Wrap third-party includes (Concert, OR-Tools) in `DREAMOR_BEGIN_THIRD_PARTY_HEADERS` /
`DREAMOR_END_THIRD_PARTY_HEADERS` from `general/third_party_headers.hpp`. That way `-Wall -Wextra` (or `-Werror`)
reports only this library's code.

## Documentation that mirrors code

Several documents are mirrored by code, so they change together:

| If you change | Also change |
|---|---|
| A code block in `README.md` or `docs/guide.md` | the same block in its Korean version (`README.ko.md`, `docs/ko/guide.md`: code is identical; only prose and comments differ), and `tests/test_readme.cpp`, which compiles that code |
| A usage example in a header comment | `tests/test_docs.cpp`, which compiles those examples |
| The public API, its defaults, or a runtime exception | `docs/reference.md` and `docs/ko/reference.md`, the guide where it explains that part (`docs/guide.md`, `docs/ko/guide.md`), `skills/dreamor/SKILL.md`, `skills/dreamor/references/api.md` |
| A compile-time message | its `// expect:` line and `skills/dreamor/references/diagnostics.md` |
| What generating a set costs to compile (`set.hpp`, `plan.hpp`) | the compile-cost table in both guides (`docs/guide.md`, `docs/ko/guide.md`): `python3 tools/measure_compile_cost.py` |
| A CI job (`.github/workflows/ci.yml`) | the same job in `tools/ci_local.sh` |
| The version | `project(... VERSION ...)` in `CMakeLists.txt` (packages use `SameMinorVersion` compatibility), the `find_package(dreamor 0.2 ...)` lines in the READMEs, and the skill's title |

`compile_commands.json` (from `python3 tools/gen_compile_commands.py`) is machine-specific; do not commit it.
