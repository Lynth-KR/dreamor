# AGENTS.md

Instructions for coding agents working **on** this repository: the map, the commands, and which rule to read before a
change. The rules themselves are in `docs/dev/`, each once. For
writing models **with** dreamor, see the skill in `skills/dreamor/` (or `README.md` and `docs/`).

dreamor is a header-only C++20 modeling layer: index sets (solver-independent), variable families (`VarTable`), and
sums (`sumExpr`), with three backends, CPLEX Concert (`dreamor::concert`), OR-Tools MathOpt (`dreamor::mathopt`) and
HiGHS on its own C++ API (`dreamor::highs`).
Version 0.9.1, MPL 2.0 (releases up to 0.5.0 were Apache 2.0).

## Layout

- `include/dreamor/`: the headers a user includes - `index_set.hpp` (the set layer), `entity_vector.hpp`,
  `param_table.hpp`, `solution_table.hpp`, `format_buffer.hpp`, `logger.hpp`, and the backends `concert.hpp`,
  `mathopt.hpp`, `highs.hpp`.
- `include/dreamor/internal/`: what they are built from, in layers - `base/`, then `index_set/`, then `backend/`, then a
  backend's own parts (`highs/`) - each including only those below it (`tools/check_layers.py`). Public names are
  defined here, as libstdc++ defines `std::vector` in `bits/`: `dreamor::IndexSet` is in `internal/index_set/`.
- `tests/`: unit, stress (differential against a plain-STL reference: one chain over many data, the engine's
  situations over shapes), docs, examples, compile_fail, contract_fail, and each backend's; `tests/stub/` stands in for Concert, and `tests/support/` holds what every kind shares.
- `examples/`: the lot-sizing model on each backend, and three OR-Tools samples ported.
- `bench/`: benchmarks, not run in CI.
- `docs/`: the users' documentation; `docs/dev/`: the rules for working on dreamor, and `architecture.md`, what each
  folder and header holds.
- `skills/dreamor/`: the Agent Skill for users writing dreamor models.
- `tools/`, `ci/`, `.github/`: the scripts, the CI's jobs and their local run, the workflow.

## Build and test

The library is headers alone; the tests are built by CMake (`tests/CMakeLists.txt`, under `DREAMOR_BUILD_TESTS`)
and run by CTest, the compile-time checks through `tests/cases.py`. They need CMake 3.25 and Python 3.12.
`CMakePresets.json` holds a preset for each CI job; the build goes to `build/<preset>`.

```bash
cmake --workflow --preset stub-gcc13-release  # one CI job as CI runs it: configure, build, test (--list-presets)
cmake --preset clang-latest && cmake --build --preset clang-latest && ctest --preset clang-latest -R chain
                                     # the steps apart, to run some tests: -R a name, -L a kind (unit, compile_fail)
CXX=clang++-18 cmake -B build -DDREAMOR_BUILD_TESTS=ON && cmake --build build -j && ctest --test-dir build -j
                                     # no preset: stub mode with $CXX, no build type (-O0, the debug checks on)
  -DDREAMOR_CPLEX_DIR=DIR            # real CPLEX Studio (the cplex preset reads $CPLEX_STUDIO_DIR)
  -DDREAMOR_ORTOOLS_DIR=DIR          # OR-Tools C++ prebuilt: the MathOpt backend, solving with HiGHS (mathopt-*)
  -DDREAMOR_HIGHS_DIR=DIR            # a HiGHS install; the OR-Tools prebuilt is one (mathopt-* set both)
  -DDREAMOR_SANITIZE=address         # Address + UndefinedBehavior + Leak; thread for ThreadSanitizer
  -DDREAMOR_TESTS="unit;stress"      # the kinds: unit stress docs examples syntax compile_fail contract_fail (default all)
cmake --workflow --preset wasm       # WebAssembly with Emscripten, under Node.js, in the emsdk image (`docs/dev/platforms.md`)
tools/format.sh                      # clang-format (C++) and ruff (Python) in place; --check changes nothing
python3 tools/check_layers.py        # the headers keep their layers (Layout); reads the includes, compiles nothing
tools/tidy.sh                        # clang-tidy; needs compile_commands.json (tools/gen_compile_commands.py)
python3 tools/check_bench.py         # the benchmarks compile, every variant, syntax only (CI runs it; bench/ runs by hand)
tools/site.sh                        # the documentation site (zensical.toml) into site/; `serve` previews it
ci/local.py [REV]                    # what CI runs, on one commit checked out apart; records a pass for the hook
```

- **CI** is `docs/dev/ci.md`: the jobs, what they cover, their images and caches, how to bump them, and how to run
  them locally. It runs on pushes to main and on pull requests, not on a change of Markdown alone but for the README
  and the site's pages under `docs/`; a work branch runs
  it by hand (`gh workflow run tests --ref <branch>`). A job is a preset of `CMakePresets.json`, run from two
  places - `.github/workflows/ci.yml` and `ci/local.py` - and described once, in `docs/dev/ci.md`: change them together.
- **Local CI** (`ci/local.py`) runs every job over one commit checked out apart, and records a pass by the
  commit's tree; `ci/hooks/pre-push` stops a push to main whose tree has none. Run it before pushing to main. Turn
  the hook on per clone with `git config core.hooksPath ci/hooks`.
- **Done means** stub mode passes on both compilers, `DREAMOR_ORTOOLS_DIR` passes if you touched anything the
  MathOpt backend or the shared machinery uses, `DREAMOR_HIGHS_DIR` passes if you touched anything the HiGHS backend
  or the shared machinery uses, the `wasm` preset passes, the windows and macos jobs pass if you touched
  platform-dependent code (`docs/dev/platforms.md`, "Checking"), `tools/format.sh --check` and `tools/check_layers.py`
  pass, and `tools/tidy.sh` reports nothing. What goes to main passes every CI job.

## Before a change

1. Find every row of the table below that applies - by the paths you will edit and by what you are doing. Several
   rows may apply to one change.
2. Read every document they name before editing.
3. Keep their rules throughout the change.
4. When the change reaches further - another path, another kind of work - read what the new rows name before going on.

| Before you change | Read |
|---|---|
| Anything in `include/` | `docs/philosophy.md` (the principles), `docs/dev/design.md` (what they decide for a change, backend parity), `docs/dev/code-style.md` ("Library code"), `docs/dev/comments.md` |
| A backend, or what every backend shares (`internal/backend/`) | `docs/dev/design.md`, "Backend parity" |
| Code that differs by platform: an `#ifdef`, a builtin, a CRT call, a `thread_local`, an integer's width | `docs/dev/platforms.md` |
| A compile-time diagnostic or a debug-build contract check | `docs/dev/diagnostics.md` |
| A test, a test's warnings, the Concert stub | `docs/dev/testing.md` |
| The set engine, the chain or set-builder notation (`set.hpp`, `relational.hpp`, `set_builder.hpp`, `element.hpp`, `binding.hpp`), how a value fills a column (`toColumn`), an index (`flat_index.hpp`, `grouped.hpp`, `row_index.hpp`), or a differential test | `docs/dev/differential-test.md` |
| A CI job, a pinned compiler, tool or image | `docs/dev/ci.md`; `docs/dev/code-style.md`, "Tools", for the formatter and clang-tidy |
| What the benchmarks exercise: a chain's `build()`, `groupBy`, `VarTable` construction or lookup, `sumExpr`, the set algebra, `Ordered`, `EntityVector`, `SolutionTable`, `ParamTable` | `docs/dev/benchmark.md` |
| Model code: the code blocks of the README, the guide's code (`tests/docs/`), `examples/`, the benchmarks' models | `docs/dev/code-style.md`, "Model code in this repository" |
| A new header, a new `#include`, a file moved or split | `docs/dev/architecture.md`, and `docs/dev/code-style.md`, "Library code" |
| The release notes | `docs/dev/releases.md` |
| A guide page or its code (`docs/guide/`, `tests/docs/guide/`, `tests/docs/highs/`) | `docs/dev/guide.md`; `docs/dev/code-style.md`, "Model code in this repository" |

## Documentation that mirrors code

Several documents are mirrored by code, so they change together:

| If you change | Also change |
|---|---|
| The README's program, or the lines it prints | `docs/guide/at-a-glance.md`, if the program types a name it does not list; nothing else: `tests/docs/readme.py` takes the program out of `README.md` and checks the excerpt above it and the ```text block after it; a HiGHS preset builds and runs it |
| `skills/dreamor/SKILL.md` | Nothing: it holds no code, and points into the copies, `skills/dreamor/docs/`, and `references/diagnostics.md` - keep the pages it names there |
| A guide page's code, or the guide's model (`tests/docs/guide/`, `tests/docs/highs/`) | The pages that pull its regions in (`--8<--` lines naming the file), their sentences about it, and `tools/site.sh` |
| A guide or reference page (`docs/guide/`, `docs/reference/`), or the code a guide page pulls in | The skill's copies, `skills/dreamor/docs/`: `python3 tools/sync_skill_docs.py` writes them, never by hand |
| The public API, its defaults, or a runtime exception | its `///` block (`docs/dev/comments.md`) and `docs/reference/`, which `python3 tools/gen_reference.py --out docs/reference` makes from it, then the skill's copies, `python3 tools/sync_skill_docs.py`; the guide page the section's `Guide:` line names, `skills/dreamor/SKILL.md` |
| A compile-time message | its `// expect:` line and `skills/dreamor/references/diagnostics.md` |
| What generating a set costs to compile (`set.hpp`, `plan.hpp`) | the compile-cost table in `docs/guide/cost.md`: `python3 tools/measure_compile_cost.py` |
| A CI job (`.github/workflows/ci.yml`, its preset in `CMakePresets.json`) | the same job in `ci/local.py`, and `docs/dev/ci.md` |
| The version | `project(... VERSION ...)` in `CMakeLists.txt` (packages use `SameMinorVersion` compatibility), the `find_package(dreamor 0.9 ...)` lines in the README, the skill and `tests/cmake_consumer*`, and the skill's title |
