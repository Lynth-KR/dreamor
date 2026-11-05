# AGENTS.md

Instructions for coding agents working **on** this repository. For writing models **with** dreamor, see the skill in
`skills/dreamor/` (or `README.md` and `docs/`). For people contributing, `CONTRIBUTING.md` says the same rules in
brief.

dreamor is a header-only C++20 modeling layer: index sets (solver-independent), variable families (`VarTable`), and
sums (`sumOf`), with three backends, CPLEX Concert (`dreamor::concert`), OR-Tools MathOpt (`dreamor::mathopt`) and
HiGHS on its own C++ API (`dreamor::highs`).
Version 0.7.1, MPL 2.0 (releases up to 0.5.0 were Apache 2.0).

## Layout

| Path | What |
|---|---|
| `include/dreamor/` | The headers a user includes, and nothing else: `index_set.hpp` (the set layer: `Col`, `makeSet`, `interval`, `IndexSet`, `Grouped`, `Ordered`, set algebra, `indexedName`; brings `entity_vector.hpp` too), `entity_vector.hpp` (`dreamor::EntityVector`, the model's entities, each once by its primary key), `value_table.hpp` (`dreamor::ValueTable`, shared by every backend), `format_buffer.hpp` (`FormatBuffer`, `NameBuilder`), `logger.hpp`, and the backends `concert.hpp` (Concert), `mathopt.hpp` (MathOpt), `highs.hpp` (HiGHS, on its own C++ API). A backend header brings `index_set.hpp`, a guarantee, as `<iostream>` brings `<istream>` and `<ostream>`. None but the backends includes a solver header |
| `include/dreamor/internal/` | What the public headers are built from, never included by a user - libstdc++'s `bits/`, Eigen's `src/` - in three layers, each including only those below it: `base/` (`tuple_hash.hpp`, `flat_index.hpp`, `aligned_vector.hpp`, `third_party_headers.hpp`, `unreachable.hpp`, `inline.hpp`), `index_set/` (the set layer's parts, behind `index_set.hpp`), `backend/` (what every backend shares: `var_table_core.hpp`, `row_index.hpp`, `sum_builder.hpp`, `inline_vector.hpp`). `tools/check_layers.py` holds the order, in CI's lint: `logger.hpp` and `base/` lowest, then `format_buffer.hpp`, `index_set/` with `entity_vector.hpp`, `index_set.hpp`, `backend/` with `value_table.hpp` (which use each other), the backends; only a backend includes a solver's header, and no user-facing code includes `internal/` |
| `examples/` | Lot-sizing example: `production_planning_data.hpp` (the data, no dreamor), `production_planning_sets.hpp` (columns and sets, solver-free), `production_planning.hpp` (Concert), `production_planning_mathopt.hpp` (MathOpt), `production_planning_highs.hpp` (HiGHS), `production_planning_plain.hpp` (without dreamor, for comparison). `mathopt_samples/`: three OR-Tools MathOpt samples ported to dreamor, with a walkthrough of each port (keep Google's license header and the modification notice on these files) |
| `tests/` | Framework-free tests, `tests/run_tests.sh`, Concert stub (`tests/stub/`), compile-fail cases |
| `bench/` | Benchmarks, not run in CI (`bench/README.md`). `bench/catalog/`: layer 1, each operation against hand-written code over data shapes and access patterns, a resolution IV screening design per family (lookups, grouping, set operations, slices, generation, order) and a full design of its own for entities and solution values, against the lean Concert stub. `bench/scenario/`: layer 2, whole models (p-median, the model of the modeling-language benchmarks, and the lot-sizing example as written) built by dreamor and by hand up to the solver's start, on the stub, CPLEX, MathOpt and HiGHS. `bench/common/`: the measurement header, the run's JSON result, the lean stub, the machine helpers, the per-repetition code layouts, `--reuse`, the shared build steps, and an MPS comparison. `bench/solver/`: how Concert's variable creation meets the caller's code. `bench/plan/`: how `makeSet`'s visit order compares with every other order - a cost model over made-up sets of 3 to 16 sources, and a timed three-source experiment |
| `skills/dreamor/` | Agent Skill for users writing dreamor code |
| `README.md`, `docs/` | Documentation, in English alone (one version to keep true): the README introduces and maps; `docs/philosophy.md` the principles and rules of use, `docs/guide.md` every part in use, `docs/reference.md` the API in tables; `docs/ci.md` the CI, for people working on dreamor |
| `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `NOTICE` | For people contributing, the Contributor Covenant 2.1, how to report a vulnerability privately, the copyright and attribution notice |
| `cmake/`, `CMakeLists.txt` | `dreamor::dreamor` INTERFACE target and install/package config |
| `tools/` | `format.sh`, `check_layers.py`, `tidy.sh`, `ci_local.sh`, `hooks/pre-push`, `clang-format` (the pinned one, for an editor), `gen_compile_commands.py`, `gen_arity_tests.py`, `measure_compile_cost.py`, `diagnostics_report.py` (how readable the compiler's errors are for the compile_fail mistakes), `tree.sh` (the repository as a tree, without what git ignores), `windows_check.sh` (the Windows check, from WSL), `containers/` (images `ci_local.sh` builds for itself) |
| `.github/` | The CI workflow, issue forms and the pull request template |
| `assets/` | The README's banner and slogan |

## Build and test

There is nothing to build; the tests do not go through CMake.

```bash
tests/run_tests.sh                   # default; needs nothing but a C++20 compiler
tests/run_tests.sh --cplex [DIR]     # real CPLEX Studio ($CPLEX_STUDIO_DIR, default /opt/ibm/ILOG/CPLEX_Studio2212)
tests/run_tests.sh --mathopt [DIR]   # OR-Tools C++ prebuilt ($ORTOOLS_DIR, default /opt/ortools), solves with HiGHS
tests/run_tests.sh --highs [DIR]     # a HiGHS install ($HIGHS_DIR, default /opt/highs); the OR-Tools prebuilt is one
CXX=clang++-18 tests/run_tests.sh    # compiler choice; default g++
OPT="-O3 -DNDEBUG" tests/run_tests.sh # optimization; default -O1 -g
SAN=1 tests/run_tests.sh             # Address + UndefinedBehavior + Leak sanitizers (stub mode only)
SAN=thread tests/run_tests.sh        # ThreadSanitizer, a build of its own (stub mode only)
JOBS=4 tests/run_tests.sh            # parallel compiles; default every core (tests still report in order)
COMPILE_CHECKS=0 tests/run_tests.sh  # skip compile_fail, syntax checks, documented-code tests: compiler-only
UNIT_TESTS=0 tests/run_tests.sh      # the compiler's checks alone: no other tests, no contract_fail
WASM=1 tests/run_tests.sh            # WebAssembly with Emscripten, under Node.js (the emsdk image, see "Platforms")
CXX=clang++-18 STDLIB=libc++ tests/run_tests.sh  # LLVM's libc++ of that Clang's version (stub mode only)
tools/format.sh                      # clang-format (C++) and ruff (Python) in place; --check changes nothing
python3 tools/check_layers.py        # the headers keep their layers (Layout); reads the includes, compiles nothing
tools/tidy.sh                        # clang-tidy; needs compile_commands.json (tools/gen_compile_commands.py)
tools/ci_local.sh [REV]              # what CI runs, on one commit checked out apart; records a pass for the hook
```

- **Default mode** runs every `tests/test_*.cpp` against the Concert stub, checks every `tests/compile_fail/*.cpp`,
  runs every `tests/contract_fail/*.cpp` (not under `-DNDEBUG` or `SAN=thread`), and syntax-checks
  `tests/cplex/*.cpp` and the Concert example.
  - `compile_fail/makeset_too_many_columns*.cpp` pin the 65th column's message. They are generated by
    `tools/gen_arity_tests.py` - edit the script and re-run it, never the files.
  - `test_differential*.cpp` (five, one per row layout: `std::size_t` columns, 4- and 8-byte columns in turn,
    entity pointers, a `std::string` column, `std::uint32_t` columns), a stress test, check the set layer and
    sumOf against a plain-STL reference (`tests/differential.hpp`) over a few hundred random cases from a fixed
    seed. A change to the semantics of rows (order, repeats, grouping,
    neighbours) has to change the reference too.
- **`--mathopt`** runs `tests/mathopt/*.cpp`, the MathOpt example, the ported samples in
  `examples/mathopt_samples/`, and `tests/mathopt/compile_fail/`.
  `test_cross_backend.cpp` builds the example on both backends and compares the constraints.
- **`--highs`** runs `tests/highs/*.cpp` (solving with HiGHS, and the README's and `highs.hpp`'s code as written
  there), the HiGHS example, `tests/highs/compile_fail/` and, in a debug build, `tests/highs/contract_fail/`.
  `test_cross_backend.cpp` there builds the example on Concert (the stub) and HiGHS and compares the constraints.
- **`--cplex`** solves with real CPLEX. CPLEX is not on CI, so run it locally when you touch Concert-facing code
  and have CPLEX available.
- **Warnings** are what dreamor's own code is held to; the CMake target adds none, so users keep their own flags.
  The library, the tests, and the examples build with `STRICT` in `tests/run_tests.sh` (`-Wconversion`,
  `-Wsign-conversion`, `-Wold-style-cast`, `-Wfloat-equal`, ... and `-Werror`): a header that warns there would warn
  in a user's strict build. The ported samples and the compile_fail cases build with `-Wall -Wextra` only - a
  warning must not become a compile_fail case's first error. Tests compare doubles with `exactlyEqual` /
  `isExactlyZero` from `tests/float_compare.hpp`, never with `==`.
- **CI** is `docs/ci.md`: the jobs, what they cover, their images and caches, how to bump them, and how to run
  them locally. It runs on pushes to main and on pull requests, not on a change of Markdown alone; a work branch runs
  it by hand (`gh workflow run tests --ref <branch>`). A job is written twice - `.github/workflows/ci.yml` and
  `tools/ci_local.sh` - and described once, in `docs/ci.md`: change the three together.
- **Local CI** (`tools/ci_local.sh`) runs every job over one commit checked out apart, and records a pass by the
  commit's tree; `tools/hooks/pre-push` stops a push to main whose tree has none. Run it before pushing to main. Turn
  the hook on per clone with `git config core.hooksPath tools/hooks`.
- **Compiler support** is a range tested at both ends, nothing in between: the floor is GCC 13 / Clang 18, with
  LLVM's libc++ 18 beside libstdc++ (raising it is a decision, not a cleanup); the ceiling is the newest releases,
  bumped at each major (`docs/ci.md`, "Bumping"). A new warning or a changed first error there is what users of that
  compiler will see: fix it, then bump. Emscripten is not a range: one version is pinned, and moving it is a tool
  update.
- **Formatting** is `.clang-format` (clang-format 23.1.3) and `ruff.toml` (ruff 0.17.1), pinned because output differs
  between versions. The scripts run the pinned versions through `uvx`, so all they need is
  [uv](https://docs.astral.sh/uv/); nothing is installed on PATH, and another project's pins do not collide.
  `examples/mathopt_samples/` is left as ported, and the code blocks of the README and `docs/` are formatted by
  hand. The script also adds the SPDX license line where it is missing.
  CI runs `tools/format.sh --check`. To bump a formatter, change its pin (`CLANG_FORMAT_VERSION` in `tools/format.sh`,
  `required-version` in `ruff.toml`), run `tools/format.sh`, commit only the reformatting, and add that commit to
  `.git-blame-ignore-revs`. Time to bump: with CI's clang-latest job, at each LLVM major (March and September) -
  clang-format and clang-tidy move to that major's newest patch on PyPI, so the newest Clang CI tests and the
  formatter are one major. `tools/clang-format` is the pinned clang-format as one executable, for an editor's
  format-on-save (VS Code: `clang-format.executable`, which the repository's `.vscode` sets).
- **Linting** is `.clang-tidy` (clang-tidy 23.1.3, run through `uvx` as well): broad check families, with
  the few that clash with this codebase turned off and the reason written beside each. tests/ and examples/ relax
  what test and example code does on purpose; tests/stub, the compile_fail cases, the ported samples and bench/ are
  not checked. An exception to a check that stays on is `NOLINT(check): reason` - never a bare NOLINT. Review what
  `tools/tidy.sh --fix` writes: applied from several translation units at once, a fix can land twice or garble a
  call, and const goes on the right.
- **Done means** default mode passes on both compilers, `--mathopt` passes if you touched anything the MathOpt
  backend or the shared machinery uses, `--highs` passes if you touched anything the HiGHS backend or the shared
  machinery uses, `WASM=1` passes and `tools/windows_check.sh` passes with cl.exe and clang-cl if you touched
  platform-dependent code
  (see "Platforms", "Checking"), `tools/format.sh --check` and `tools/check_layers.py` pass, and `tools/tidy.sh`
  reports nothing.

## Platforms

Performance is measured, tuned and promised on Linux x86-64 with libstdc++ alone - built by GCC and by Clang, both on
libstdc++: the benchmarks, the rules under "Optimize for a mechanism", and every figure in the docs are that
platform's. On LLVM's libc++ dreamor builds and passes its tests, and is not timed. dreamor also builds and passes its tests on Windows with
clang-cl (Clang's MSVC-compatible driver, at Clang's floor, 18), on the MSVC STL and the UCRT, and on WebAssembly
(wasm32) with Emscripten; how fast it runs there is not measured. MSVC's own compiler, cl.exe (Visual Studio 2022), is
being brought up: the default-mode tests build without a warning from dreamor's headers at `/W4` and pass with
Visual Studio 2022 17.14, but the oldest version it supports and the rest (the CMake package, the solvers'
backends) are not settled, so the user-facing documents do not claim it yet.

- **Portable code over one platform's fast path.** What one instruction set alone has - SIMD (`<immintrin.h>`,
  `_mm_*`, AVX2), BMI, target attributes, inline assembly - is not used: it splits an algorithm by CPU and widens the
  gap between platforms. An operation every 64-bit CPU has, which compilers only spell differently, is fine in one
  helper with a fallback in standard C++ that gives the same result: `internal::foldedMultiply` takes the high
  half of a 64x64 product as `__int128` (GCC, Clang) or `__umulh` (MSVC, `<intrin.h>`), and in 32-bit halves
  elsewhere (`foldedMultiplyPortable`, which `tests/test_tuple_hash.cpp` holds against the others);
  `internal::unreachable()`; `FlatIndex::prefetchForWrite` (no prefetch where there is no builtin).
- **The standard library's common ground.** Use what every supported implementation has at its floor - libstdc++ 13,
  libc++ 18, the MSVC STL of Visual Studio 2022 - and nothing that one of them adds or lacks. Where a feature outside
  that ground has one inside it that does the same at the same speed, take the inside one:
  - `std::thread` and `join()`, not `std::jthread`, which libc++ 18 has only behind `-fexperimental-library`;
  - a `std::formatter` whose `format` is a template on its context, not one taking `std::format_context&`, which
    libc++ 18's `formattable` rejects (it checks with a context of its own).
- **Platform differences live behind macros, one small helper per difference.** The helper holds the
  `#ifdef _WIN32` / `#else` / `#endif` and its callers do not branch: `isTerminal()`, `isEnvSet()` and `openFile()`
  in `logger.hpp` are the pattern. No `#ifdef` inside a function's logic, and no second implementation
  of an algorithm under a macro.
- **Choose the macro by what differs.**
  - The operating system or C runtime (POSIX against Win32/UCRT functions, `<unistd.h>` against `<io.h>`): `_WIN32`.
  - The compiler (builtins, pragmas, attributes): `__clang__` and `__GNUC__`, tested before `_MSC_VER`, because
    clang-cl defines `_MSC_VER` too; cl.exe is the `_MSC_VER` branch (`internal/base/unreachable.hpp`,
    `internal/base/inline.hpp`, `internal/base/third_party_headers.hpp`). An attribute is one of these too: cl.exe warns about a
    `gnu::` attribute at every warning level (C5030), so `DREAMOR_INLINE` and `DREAMOR_NOINLINE` stand for
    `gnu::always_inline` and `gnu::noinline`. A `#pragma GCC` or `#pragma clang` outside such a helper is an unknown
    pragma to cl.exe (C4068), in a test too: `tests/float_compare.hpp` lets `std::equal_to` do its `==` instead.
  - The standard library: no macro. Write code that holds on every implementation.
- **Rely on the standard, not on libstdc++'s or libc++'s choices.** What breaks under the MSVC STL:
  - an iterator assumed to be a pointer: `std::string_view::begin()` returns a class there, so name the type
    (`std::format_parse_context::iterator`), never `const auto*` - nor bare `auto`, which `readability-qualified-auto`
    turns back into `const auto*` where the iterator is a pointer;
  - `std::filesystem::path::c_str()` is `const wchar_t*`; pass `path.string().c_str()` to a `const char*`;
  - `long` is 32 bits (LLP64), so `long` and `IloInt` (`__int64`) are different types and a `long` times a Concert
    variable is ambiguous - one more reason for the explicit-size types under "Code style".
- **cl.exe's preprocessor.** Its default, the traditional one, has no `__VA_OPT__` (`/Zc:preprocessor` is the
  user's flag to set, not dreamor's): a variadic macro takes `...` alone and passes `__VA_ARGS__` on whole, as the
  `DREAMOR_LOG_*` macros do with their format string.
- **Code files are ASCII, comments included.** cl.exe reads a file without a byte-order mark in the system's code
  page unless the user passes `/utf-8`: on a Korean Windows (CP949) a UTF-8 character is warning C4819 in every
  build that includes the header. Write math in AMPL's words (`union`, `inter`, `sum {(i,k) in LINKS}`).
- **A 32-bit `std::size_t` (wasm32).** A value that needs 64 bits says `std::uint64_t`, not `std::size_t`. A key
  of integers, enums or columns of them hashes the same on every platform: `hashElement` hands each element over
  in 64 bits (a column through its own overload, found by argument-dependent lookup), `TupleHash` keeps a 64-bit
  state and a 32-bit `std::size_t` holds its low half; `tests/test_tuple_hash.cpp` pins a few keys' hashes.
- **A `thread_local` at namespace scope is `constinit`.** Under the MSVC ABI (cl.exe and clang-cl) a dynamic
  initializer of one runs from a TLS callback at the start of every thread - the loader's worker threads too, before
  the CRT has set up stdio - where a CRT call can end the process before `main`, now and then (the logger's
  `_isatty(_fileno(stderr))` did, in about one run in twenty). GCC and Clang elsewhere initialize it on first use, so
  only Windows shows it. Hold a constant (`nullptr` for stderr, an unknown state) and compute on first use, as
  `logger.hpp` and `internal/backend/row_index.hpp` do; a `thread_local` inside a function is initialized on
  first use everywhere.
- **The MSVC CRT's deprecation warnings count** (`fopen`, `getenv`, ... under `-Wdeprecated-declarations`): a header
  that warns there warns in a user's Windows build. Call the CRT's own spelling in the helper's `_WIN32` branch
  (`_dupenv_s` for `getenv`; `_fsopen` for `fopen`, since `fopen_s` opens the file unshared); never define
  `_CRT_SECURE_NO_WARNINGS` in a header, where it would reach the user's code.
- **Checking.** The standard library's common ground is the libcxx job's (libc++ 18 and the newest) beside the
  GCC and Clang jobs' libstdc++; the MSVC STL is checked with Windows, below. WebAssembly is CI's wasm job: the emsdk
  image at one pinned version (`docker.io/emscripten/emsdk:6.0.13`, in `ci.yml` and `tools/ci_local.sh`), for
  Emscripten whose LLVM and libc++ are 18 or later; the HiGHS backend there is the wasm-highs job's, with HiGHS
  built by `tools/containers/build-highs-wasm.sh`. HiGHS's simplex and MIP need a stack of 8 MB there
  (`-sSTACK_SIZE=8MB`, which `WASM=1` gives the tests): Emscripten's default of 64 KB overflows, unchecked, into
  the memory around it.
  Locally: `podman run --rm -v "$PWD":/src -w /src docker.io/emscripten/emsdk:6.0.13 sh -c 'WASM=1
  tests/run_tests.sh'`. Windows is not on CI. `tools/windows_check.sh [cl|clang-cl]`, from WSL with Visual Studio 2022
  installed (clang-cl is its "C++ Clang tools for Windows" component), builds the default-mode tests
  (`tests/test_*.cpp` with `-Itests/stub`) at `/W4`, runs each several times (`RUNS`; a failure before `main` need
  not come every run), and lists every warning from `include/`; run it with both compilers. Or, by hand,
  clang-cl from WSL with
  `clang-cl-23 --target=x86_64-pc-windows-msvc -fuse-ld=lld-link /std:c++20 /EHsc`, the MSVC STL and the Windows
  SDK's `ucrt`, `um` and `shared` headers under `/mnt/c` given with `/imsvc`, their `lib/x64` directories after
  `/link` with `/libpath:`, and the `.exe` run through WSL interop. The compile_fail and contract_fail cases are not
  checked on Windows.

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

These behave identically on every backend. A change to one is a change to all:

- bounds as numbers or column callables;
- variable names `<name>_<key0>_<key1>...`;
- `std::out_of_range` for a lookup outside the set;
- iteration in set order;
- `sumOf`'s `where()` / `whereEach()` / `build()`, linear or quadratic terms (`build()` gives each solver's expression
  that holds them: `IloExpr`, MathOpt's `LinearExpression` / `QuadraticExpression`, or HiGHS's
  `HighsLinearExpression` / `HighsQuadraticExpression`);
- a `VarTable` as a sum term.

Backend-only features stay in their backend:
- Concert and HiGHS, not MathOpt (whose models have none): semi-continuous variables - Concert's `semiContinuous`
  constructor, HiGHS's `kSemiContinuous` and `kSemiInteger` as the type.
- MathOpt and HiGHS: `std::logic_error` from `solution()` when there is no (primal) solution - MathOpt would
  otherwise CHECK-fail, and HiGHS would hand back the values it left from before.
- HiGHS only: `HighsEnv`, the model collected and passed to the `Highs` once; a row as a type of its own, so a
  row given as an objective and a quadratic row stop at compile time; `HighsError` for a HiGHS call that fails.

Shared logic belongs in `include/dreamor/internal/backend/`.

## Benchmark

`bench/` is not part of the test suite or CI. When a change touches what the benchmarks exercise
(`makeSet`, `groupBy`, `VarTable` construction or lookup, `sumOf`, the set algebra, `Ordered`,
`EntityVector`, `ValueTable`), check with `bench/catalog/build.sh` and `bench/scenario/build.sh`:
- both still build;
- every arm still does the same work: a quick run of each family the change reaches
  (`python3 run.py --family <family> --n-levels 100000 --reps 1 --layout off` in `bench/catalog/`, then
  `report.py`) and of a scenario (`python3 run.py --scenario lotsizing --levels 170 --reps 1 --layout off` in
  `bench/scenario/`, then `report.py`) report no check mismatch. `--layout off` because `build.sh` builds no
  per-repetition copies unless `LAYOUTS` is set.

A timing claim compares builds: the catalog's `run.py --builds base=<old>,head=<new>` and `compare.py`
over the families the change reaches, the whole experiment and not a few cells (see "Optimize for a
mechanism"), and the scenarios for a change a whole model would feel. `python3 -m unittest
common.test_machine` (in `bench/`) tests the CPU choice. An agent's sandbox is not a benchmark
machine.

## Diagnostics

Misuse is stopped at compile time with a `static_assert` whose message is one sentence saying what to write
instead (e.g. `sumOf(...) is not an expression yet: add .build() - ...`). When adding or changing a diagnostic:

1. Add a case to `tests/compile_fail/` (Concert/set layer), `tests/mathopt/compile_fail/` or
   `tests/highs/compile_fail/`. Each case is one file that starts with the license header, then a
   `// expect: <text>` line, and includes `prelude.hpp`.
2. The runner requires `<text>` in the compiler's **first** error and its notes, not just a rejection. This must
   hold on both GCC 13 and Clang 18, and on the newest releases and Emscripten's em++, whose jobs check the cases
   too. Arrange overloads so the dreamor message comes first, not a wall of template
   errors.
3. `control.cc` in each folder must keep compiling; it proves that a rejection comes from the library.
4. Update the message table in `skills/dreamor/references/diagnostics.md`.

A broken contract - a precondition such as "no source holds an element twice" - is undefined behavior, and only a
debug build checks it: `DREAMOR_DEBUG_FATAL` prints one sentence naming the contract and aborts, as libstdc++'s
assertions do. Each check has a case in `tests/contract_fail/` (`tests/highs/contract_fail/` for the HiGHS
backend's own): a program that breaks the contract, with the same
`// expect: <text>` line, which the runner looks for in what the program prints before it stops.

## Concert stub

`tests/stub/` stands in for the Concert headers so CI runs without CPLEX. It mirrors the real header layout. For
example, `IloModel` has its own header, as in real Concert, so a missing include fails here as it would against
CPLEX. If library code starts using another Concert API, add it to the stub the way the real headers declare it.
Confirm with `--cplex` when possible.

## Code style

Two sets of rules, both this repository's own: one for the library's code, one for the code that shows dreamor in
use. Neither binds a user - dreamor cannot check a user's names or style, and its `.clang-tidy` is the repository's,
not a user's. What the documentation offers users is a recommendation (`docs/philosophy.md`, "Naming").

### Library code

The library (`include/`), the tests, the tools and the benchmarks' harness. `tools/format.sh`, `tools/tidy.sh`
(`readability-identifier-naming` among its checks) and `STRICT` hold it.

Every code file starts with `// SPDX-License-Identifier: MPL-2.0` (`#` in Python, shell, and CMake; after the
`#!` line in scripts), then MPL's Exhibit A notice on the next two lines and a bare comment line before a comment
that follows, as Eigen's files do; `tools/format.sh` puts all three there and CI checks them. Google's samples in
`examples/mathopt_samples/` keep their own Apache-2.0 line and header (`examples/mathopt_samples/LICENSE`). Each header then has this convention block.
Follow it in library code:

- types `PascalCase`;
- variables `snake_case`, members with a trailing underscore (`rows_`);
- functions `camelCase`;
- constants `kPascalCase`;
- macros `SCREAMING_SNAKE_CASE`, prefixed `DREAMOR_` (a bare `CHECK` collides with Abseil and glog, see
  `tests/check.hpp`).

Namespaces say who a name is for, and where it lives:
- a public name is in `dreamor`, or in its component's namespace (`dreamor::concert`, `mathopt`, `highs`, `logger`,
  `interval`), wherever it is defined - a set-layer type defined under `internal/index_set/` is `dreamor::IndexSet`,
  as libstdc++ defines `std::vector` in `bits/`: the name a diagnostic shows and the namespace ADL searches are the
  user's;
- the internal layers are `dreamor::internal::base`, `index_set` and `backend`, after their folders, declared
  `namespace dreamor::internal::inline base` - inline namespaces of `dreamor::internal`, as
  `std::literals::chrono_literals` is of `std::literals`: code writes `internal::FlatIndex`, and GCC's diagnostics
  name the layer. Their names never collide, having been one namespace; the include order keeps the layers apart;
- a public header's own helpers are in its namespace's `detail` (`dreamor::detail`, `dreamor::highs::detail`,
  `dreamor::logger::detail`, `dreamor::interval::detail`); what several headers share belongs to a layer instead.

No namespace alias or using-directive at namespace scope: in a header it reaches the user's code.

Wrap third-party includes (Concert, OR-Tools, HiGHS) in `DREAMOR_BEGIN_THIRD_PARTY_HEADERS` /
`DREAMOR_END_THIRD_PARTY_HEADERS` from `internal/base/third_party_headers.hpp`. That way `-Wall -Wextra` (or `-Werror`)
reports only this library's code.

### Model code in this repository

The code that shows dreamor in use: the code blocks of the README and `docs/`, the usage examples in header
comments, `examples/` (but for the ported `mathopt_samples/`, left as Google wrote them), the skill's examples, and
the benchmarks' dreamor models. Review holds it; `examples/.clang-tidy` turns the library's naming check off.

- **Names.** Columns `cPascalCase`, sets `sSCREAMING_SNAKE`, variable families `vPascalCase`; callables that take
  columns in `snake_case`; everything else in `camelCase`.
- **No inline lambdas.** Name every callable - a local variable, or a `static constexpr` member or namespace-scope
  constant where there is no local scope (a constructor's member-init list) - and pass it by name: to `where`,
  `sumOf`, a VarTable's bounds, `EntityVector::modify`.
- **AMPL's idioms.** A set is defined as AMPL would write it: a relation is data the builder fills once
  (`ITEMS_OF[m]`), read through a dependent source (`{m in MACHINES, i in ITEMS_OF[m]}`), not a join that looks a
  key up again; `setof` through `select`. Rows of a set, and functions taking them, use column types (`cMachine`,
  `std::tuple<cMachine, cItem>` for rows gathered before the set exists); entity fields, primary keys, nullable
  find results and dreamor's hooks (`keyName`, a dependent source's range) stay `const T*`. Entities are owned by
  `EntityVector<T>`; a subset is a range of `const T*`. A family of rows runs over its domain: the condition that
  decides whether a row is there goes into the set the loop runs over (`S.where(not_first)`, AMPL's
  `{(i,t) in STOCK: ord(t) > 1}`), not an `if` around the row in the body nor a sentinel value standing for "no
  row"; an if-then-else inside a row, choosing a term, is AMPL's own and stays (`docs/philosophy.md`, "A family of
  rows runs over its set"). AMPL's idioms are leading cases: not binding syntax, but given substantial weight - what
  AMPL allows and its models commonly write is fine.
- **One model per backend.** The examples keep one model per backend side by side
  (`production_planning*.hpp`), though they repeat each other; they are not folded into one template over a
  backend trait. The sets are dreamor's and shared (`production_planning_sets.hpp`); each model owns its solver's
  objects and is built Sets -> VarTables -> Model.

### Both

Primitive types follow the domain, everywhere (library, tests, examples, bench):
- a count, size, or index is never signed: `std::size_t` by default, a fixed-width `std::uint32_t` (or narrower)
  where storage size matters;
- signed only where a negative value means something (negative stock, a step backwards: `std::ptrdiff_t`);
- prefer types whose size is explicit (`std::size_t`, `std::uint32_t`, `std::int16_t`, ...) over `int`, `long`,
  `unsigned`. The exceptions are what the language or a mirrored API fixes: `main(int argc, ...)`,
  `operator++(int)`, the process exit status, `IloInt` in the Concert stubs.

## Documentation that mirrors code

Several documents are mirrored by code, so they change together:

| If you change | Also change |
|---|---|
| A code block in `README.md` or `docs/guide.md` | `tests/test_readme.cpp`, which compiles that code |
| A usage example in a header comment | `tests/test_docs.cpp`, which compiles those examples |
| The public API, its defaults, or a runtime exception | `docs/reference.md`, the guide where it explains that part (`docs/guide.md`), `skills/dreamor/SKILL.md`, `skills/dreamor/references/api.md` |
| A compile-time message | its `// expect:` line and `skills/dreamor/references/diagnostics.md` |
| What generating a set costs to compile (`set.hpp`, `plan.hpp`) | the compile-cost table in `docs/guide.md`: `python3 tools/measure_compile_cost.py` |
| A CI job (`.github/workflows/ci.yml`) | the same job in `tools/ci_local.sh`, and `docs/ci.md` |
| The version | `project(... VERSION ...)` in `CMakeLists.txt` (packages use `SameMinorVersion` compatibility), the `find_package(dreamor 0.7 ...)` lines in the README, the skill and `tests/cmake_consumer*`, and the titles of the skill and its `api.md` |

`compile_commands.json` (from `python3 tools/gen_compile_commands.py`) is machine-specific; do not commit it.

## Release notes

What changed is told in the GitHub release notes alone; there is no `CHANGELOG.md`. The next version's notes collect
in its draft release. Their readers are the library's users, and they read a release's notes to decide whether to
upgrade and what upgrading takes, so the notes say that and no more, in three parts:

- **What's new**: what a user gains, a line each - new features, faster parts, and the fixes worth upgrading for.
- **Upgrading from x.y**: what code written for the version before has to change, shown in code; or one line saying
  nothing does.
- **Requirements**: compilers, solvers and libraries, only when they change.

No figures, no design reasoning, no account of how a bug came about, and no CI, benchmark, test or tooling change:
the commit messages hold all of that.
