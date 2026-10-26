# Contributing to dreamor

**English** · [한국어](CONTRIBUTING.ko.md)

Thank you for looking. dreamor is maintained by one person ([@Lynth-KR](https://github.com/Lynth-KR)), and bug
reports, questions, fixes and ideas are all welcome. Issues, pull requests and discussions are written in English.
This page says what to read first, how to report and propose, how to build and test, and how a change gets in.

A question - how to model something, why a call does not compile - goes to
[Discussions](https://github.com/Lynth-KR/dreamor/discussions); an issue is for a bug or a feature.

## Before you start

dreamor is small on purpose, and most of what makes it useful is what it does not do. Read the
[philosophy](docs/philosophy.md) before proposing a change: its principles are what every change is weighed
against. A few follow from them and are worth knowing up front:

- **No second way to do one thing.** No pipe syntax, no free-function aliases of members.
- **The solver's API is not wrapped.** Constraints, objectives, solving and duals stay in the solver's notation.
- **No user-facing knob for performance.** Speed comes from inside, without a new concept in the API.
- **Out of scope:** constraint tables, keyed dual or reduced-cost accessors, column generation, coefficient-array
  overloads, duplicate checks or deduplication of sets, constraint programming - see
  [what dreamor does not do](docs/philosophy.md#what-dreamor-does-not-do) for why.

For anything larger than a fix, open an issue first and agree on the design there: it saves a pull request that
cannot be merged.

## Reporting a bug

Open an issue with the bug report form, which asks for:

- the dreamor version (or commit), the compiler and its version, the backend (Concert or MathOpt) and its version;
- the smallest code that shows the problem - a set, a table and a few rows usually do;
- what you expected and what happened; for a compile error, its first lines.

## Proposing a feature

Use the feature request form, and confirm there that you have read the philosophy. Describe the modeling problem
first - in AMPL, if you can, as `sum {(m,i,t) in PRODUCE} ...` - and the code you write for it today. A feature is
judged by the models it makes clearer, so the problem matters more than a proposed API.

## Tests and development environment

The tests run without a framework, through a single `tests/run_tests.sh`. Choose the compiler with `CXX` (default
`g++`, C++20).

**Compilers:** GCC 13 or later, Clang 18 or later. CI tests both ends of that range - GCC 13 and 16, Clang 18 and 23 -
and nothing in between; MSVC and Apple Clang are not tested. To try the newest ones locally: Clang from
[apt.llvm.org](https://apt.llvm.org/) (`CXX=clang++-23 tests/run_tests.sh`), GCC from its official container image
(`podman run --rm -v "$PWD":/src -w /src docker.io/library/gcc:16 sh -c 'CXX=g++ tests/run_tests.sh'`; `docker` takes
the same arguments).

| Command | What | Requires |
|---|---|---|
| `tests/run_tests.sh` | The whole set layer and Concert backend (on a Concert stub), that code which must not compile is rejected, and the syntax of the CPLEX-only code | Nothing |
| `tests/run_tests.sh --cplex [DIR]` | Verification by solving with real CPLEX, running the example | CPLEX Studio (`$CPLEX_STUDIO_DIR`, default `/opt/ibm/ILOG/CPLEX_Studio2212`) |
| `tests/run_tests.sh --mathopt [DIR]` | Verification of the MathOpt backend by solving with HiGHS, running the example, building the same example with both backends and comparing the constraints, running the ported OR-Tools samples (SCIP) | OR-Tools C++ prebuilt (`$ORTOOLS_DIR`, default `/opt/ortools`) |

Code that must not compile lives one case per file in `tests/compile_fail/` (Concert) and
`tests/mathopt/compile_fail/` (MathOpt). The runner does not only check that each case is rejected: a
`// expect: <text>` line at the top of the file must appear in the compiler's **first error** (and its notes). The
point is that what a user reads before the next error is the intended one-sentence message. Cases without
`// expect:` only have to be rejected. `control.cc` in the same folder is a control case checking that the shared
prelude compiles; if it fails, the other cases would be meaningless, so it counts as a failure.

CI (GitHub Actions) runs the stub and MathOpt modes with both GCC 13 and Clang 18 (checking the `// expect:` text
with both compilers), the stub mode again at `-O3 -DNDEBUG`, under AddressSanitizer and UndefinedBehaviorSanitizer,
and with GCC 16 and Clang 23, and goes as far as installing the CMake package and consuming it via `find_package`
(`tests/cmake_consumer`, `tests/cmake_consumer_mathopt`). CPLEX is not on CI, so run `--cplex` locally.

OR-Tools needs no installation: unpack the release's C++ prebuilt anywhere and point to that folder (no CMake needed).

```bash
mkdir -p ~/opt && cd ~/opt
curl -L https://github.com/google/or-tools/releases/download/v9.15/or-tools_amd64_ubuntu-24.04_cpp_v9.15.6755.tar.gz | tar xz
export ORTOOLS_DIR=~/opt/or-tools_x86_64_Ubuntu-24.04_cpp_v9.15.6755
```

If you are not on Ubuntu 24.04, pick the C++ archive for your distribution from the releases page.

The editor setup assumes clangd. `python3 tools/gen_compile_commands.py` generates `compile_commands.json` (CPLEX and
OR-Tools paths come from the environment variables above), and the repository's `.clangd` and
`.vscode/settings.json` point at it. `compile_commands.json` differs per machine, so it is not committed.

## Making a change

- **Style.** `tools/format.sh` formats C++ (clang-format 23.1.3) and Python (ruff 0.17.1) and adds the SPDX line
  every file starts with; `tools/tidy.sh` runs clang-tidy 23.1.3 over the library, the tests and the examples, and
  must report nothing. The formatters are pinned: `uv tool install clang-format==23.1.3 clang-tidy==23.1.3
  ruff==0.17.1`. Library code names types `PascalCase`, functions `camelCase`, variables `snake_case` (members with
  a trailing `_`), constants `kPascalCase`; examples and documentation use the user conventions of the
  [philosophy](docs/philosophy.md#naming). A count, size or index is never signed.
- **Tests.** A change of behavior comes with a test. A new compile-time diagnostic comes with a case in
  `tests/compile_fail/` whose `// expect:` line names the sentence the compiler must print first, on GCC 13 and
  Clang 18 alike; a new debug-build contract check, with a case in `tests/contract_fail/`.
- **Documentation that mirrors code.** The code in the [guide](docs/guide.md) and the README is compiled by
  `tests/test_readme.cpp`, and the examples in header comments by `tests/test_docs.cpp`: change them together. A
  change to the public API changes the [API reference](docs/reference.md), its Korean version, and the agent skill
  in `skills/dreamor/`.
- **Performance.** A claim that something is faster comes with a measurement: the catalog compares two builds of
  the library run by run ([bench/README.md](bench/README.md#checking-a-change)). An optimization has to rest on
  something dreamor guarantees, and hold across the experiment, not in a few cells.
- **Done** means the default tests pass on GCC and Clang, `--mathopt` passes if the MathOpt backend or the shared
  machinery is touched, `tools/format.sh --check` passes, and `tools/tidy.sh` reports nothing.

[AGENTS.md](AGENTS.md) holds every development rule in full, written for coding agents; it is the reference when
this page is not enough.

## Commits and pull requests

- **One theme a commit.** A commit does one thing and says what and why: a subject such as `perf: unite indexes only
  its left set` or `docs: ...`, and a body with the reasoning and, for a performance change, the numbers.
- **Sign off every commit** (Developer Certificate of Origin). `git commit -s` adds a line
  `Signed-off-by: Your Name <you@example.com>`, by which you certify the [DCO](https://developercertificate.org/):
  that you wrote the change or have the right to submit it under the project's license, Apache 2.0. A pull request
  whose commits are not signed off cannot be merged.
- **CI** runs on every pull request: formatting, clang-tidy, the tests on GCC 13, 16 and Clang 18, 23, the
  sanitizers, the MathOpt backend, and the CMake package. CPLEX is not on CI; if you touch Concert-facing code and
  have CPLEX, run `tests/run_tests.sh --cplex`.
- The maintainer may squash or reword commits so that the history reads by theme.

## Code of conduct

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).
