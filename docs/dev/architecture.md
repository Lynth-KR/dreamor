# Architecture

For people working on dreamor: what each folder and header holds. `AGENTS.md` gives the short map; the rules are the
other files here.

## Headers

- `include/dreamor/`: The headers a user includes, and nothing else: `index_set.hpp` (the set layer: the columns -
  `DREAMOR_ENTITY_COLUMN`, `DREAMOR_VALUE_COLUMN` - `index_set::relational::from` and its chain,
  `index_set::set_builder` (`of`, `in`), `interval`, `IndexSet`, `Grouped`, `Ordered`, set algebra, `indexedName`;
  brings `entity_vector.hpp` too), `entity_vector.hpp` (`dreamor::EntityVector`, the model's entities, each once by its
  primary key), `solution_table.hpp` (`dreamor::SolutionTable`, what every backend's `solution()` gives back),
  `param_table.hpp` (`dreamor::ParamTable`, data on a set), `format_buffer.hpp` (`FormatBuffer`, `NameBuilder`),
  `logger.hpp`, and the backends `concert.hpp` (Concert), `mathopt.hpp` (MathOpt), `highs.hpp` (HiGHS, on its own C++
  API). A backend header brings `index_set.hpp`, a guarantee, as `<iostream>` brings `<istream>` and `<ostream>`. None
  but the backends includes a solver header.
- `include/dreamor/internal/`: What the public headers are built from, never included by a user - libstdc++'s `bits/`,
  Eigen's `src/` - in three layers, each including only those below it: `base/` (`tuple_hash.hpp`, `flat_index.hpp`,
  `aligned_vector.hpp`, `primary_key.hpp`, `third_party_headers.hpp`, `unreachable.hpp`, `inline.hpp`, `lifetimebound.hpp`), `index_set/`
  (the set layer's parts, behind `index_set.hpp`), `backend/` (what every backend shares: `var_table_core.hpp`,
  `row_index.hpp`, `row_values.hpp`, `sum_builder.hpp`, `inline_vector.hpp`), and `highs/`, the HiGHS backend's parts
  behind `highs.hpp` (`error.hpp`, `expression.hpp`, `operators.hpp`, `environment.hpp`, `model.hpp`).
  `tools/check_layers.py` holds the order, in CI's lint: `logger.hpp` and `base/` lowest, then `format_buffer.hpp`,
  `index_set/` with `entity_vector.hpp`, `index_set.hpp`, `backend/` with `solution_table.hpp` and `param_table.hpp`
  (which use each other), the backends with their parts; only a backend and its parts include a solver's header, and
  no user-facing code includes `internal/`.

## Tests and examples

- `tests/`: Framework-free tests, built by `tests/CMakeLists.txt` and run by CTest, with `tests/cases.py` for the
  compile-time checks: `unit/` (one part each, and `test_usage.cpp`, each public part used once as a model does),
  `stress/` (the differential tests against a plain-STL reference, lookups against hints that point anywhere, and
  allocations failing one at a time, and the set engine's situations over shapes),
  `docs/` (the guide's code, which its pages pull in - `guide/`, its model `guide/model.hpp`, and `highs/`, what
  solves, its plan `highs/plan.hpp` - with `log_capture.hpp` for the tests that read the log; the README's program is
  taken out of it by `docs/readme.py`), `examples/` (the example's model against the same model written by hand),
  `compile_fail/` and `contract_fail/` (cases in topic files), the backends' own (`mathopt/`, `highs/`, `cplex/`),
  the Concert stub (`stub/`), the CMake consumers, and `support/`, what every kind shares: `check.hpp` (the checks
  and the report), `float_compare.hpp`, `failing_allocation.hpp` (an operator new that fails at the N-th allocation or at one of
  a size), `column_values.hpp` (an index as a value of each column type the random tests
  use, and back), and `cross_backend.hpp` (the random model each backend's
  `test_cross_backend_random.cpp` builds on it and on the stub).
- `examples/`: Lot-sizing example: `production_planning_data.hpp` (the data, no dreamor), `production_planning_sets.hpp`
  (columns and sets, solver-free), `production_planning_concert.hpp` (Concert), `production_planning_mathopt.hpp`
  (MathOpt), `production_planning_highs.hpp` (HiGHS), `production_planning_plain.hpp` (without dreamor, for comparison).
  `mathopt_samples/`: three OR-Tools MathOpt samples ported to dreamor, with a walkthrough of each port (keep Google's
  license header and the modification notice on these files).
- `bench/`: benchmarks, not run in CI; `docs/bench/index.md` says what each part measures and how.

## Everything else

- `skills/dreamor/`: Agent Skill for users writing dreamor code: `SKILL.md` (where to look, the order of a model, the
  rules agents break, a checklist), and `docs/`, copies of the guide and the reference - its compile-time messages,
  exceptions and debug-build checks included - that `tools/sync_skill_docs.py` writes.
- `README.md`, `docs/`: Documentation, in English alone (one version to keep true): the README introduces and maps;
  `docs/philosophy.md` what dreamor holds to and why, `docs/guide/` how to do each thing, a page a topic, `docs/reference/` the API, a page a section, generated from the headers' `///` comments; `docs/dev/` the rules for people working on dreamor, a file a topic. `zensical.toml` makes a site of `docs/` but
  `docs/dev/` (`tools/site.sh`).
- `CONTRIBUTING.md`: How to ask, report and propose, where the rules for working on the code are, and how commits get in.
- `CODE_OF_CONDUCT.md`, `SECURITY.md`, `NOTICE`: The Contributor Covenant 2.1, how to report a vulnerability privately,
  the copyright and attribution notice.
- `LICENSE`, `LICENSES/`, `REUSE.toml`: The licenses (README.md, "License"): the MPL's text, which GitHub reads, the
  text of each license in use, and the copyright and license of every file that does not say them itself, which
  `tools/format.sh` checks with `reuse lint`.
- `cmake/`, `CMakeLists.txt`: `dreamor::dreamor` INTERFACE target and install/package config.
- `tools/`: `format.sh`, `check_layers.py`, `gen_reference.py` (the API reference from the headers' `///` comments, by `refgen/`; its
  sections' order in `reference_sections.txt`), `tidy.sh`, `clang-format` (the pinned one, for an editor),
  `gen_compile_commands.py`, `gen_arity_tests.py`, `measure_compile_cost.py`, `diagnostics_report.py` (how readable the
  compiler's errors are for the compile_fail mistakes), `tree.sh` (the repository as a tree, without what git ignores).
- `ci/`, `CMakePresets.json`: The CI's jobs, a preset each, and `ci/local.py` (the same jobs on this machine, recording
  a pass for `hooks/pre-push`), `containers/` (images `local.py` builds for itself).
- `.github/`: The CI workflow, issue forms and the pull request template.
- `docs/images/`: The banner, the README's and the site's front page's.
- `docs/stylesheets/`: The site's own styles on top of Zensical's theme (`zensical.toml`, `extra_css`).
