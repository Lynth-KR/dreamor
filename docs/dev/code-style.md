# Code style

For people working on dreamor; `AGENTS.md` says when to read it.

Two sets of rules, both this repository's own: one for the library's code, one for the code that shows dreamor in
use. Neither binds a user - dreamor cannot check a user's names or style, and its `.clang-tidy` is the repository's,
not a user's. What the documentation offers users is a recommendation.

### Library code

The library (`include/`), the tests, the tools and the benchmarks' harness. `tools/format.sh`, `tools/tidy.sh`
(`readability-identifier-naming` among its checks) and `STRICT` hold it.

In `include/` - the code dreamor is made of, and only there - primitive types follow the domain:
- a count, size, or index is never signed: `std::size_t` by default, a fixed-width `std::uint32_t` (or narrower)
  where storage size matters;
- signed only where a negative value means something (negative stock, a step backwards: `std::ptrdiff_t`);
- prefer types whose size is explicit (`std::size_t`, `std::uint32_t`, `std::int16_t`, ...) over `int`, `long`,
  `unsigned`. The exceptions are what the language or a mirrored API fixes: `main(int argc, ...)`,
  `operator++(int)`, the process exit status.

<!-- REUSE-IgnoreStart -->
Every code file starts with `// SPDX-License-Identifier: MPL-2.0` (`#` in Python, shell, and CMake; after the
`#!` line in scripts), then MPL's Exhibit A notice on the next two lines and a bare comment line before a comment
that follows, as Eigen's files do; `tools/format.sh` puts all three there and CI checks them. The code that the
documents show and users copy - `examples/` and the guide's code, `tests/docs/guide/` and `tests/docs/highs/` - starts
with `// SPDX-License-Identifier: MIT-0` alone. Google's samples in `examples/mathopt_samples/` keep their own
Apache-2.0 line and header (`examples/mathopt_samples/LICENSE`). Every other file - a document, a configuration
file, an image - takes its license from `REUSE.toml`, and `tools/format.sh` runs `reuse lint` over all of them.
<!-- REUSE-IgnoreEnd -->

Comments - what a `//` comment says and what it leaves to the guide, the reference or the commit message, and the
format of the `///` blocks the API reference is generated from - are `comments.md`, and only there.

The names in library code, which `readability-identifier-naming` checks - written here once, not in each header:

- types `PascalCase`;
- variables `snake_case`, members with a trailing underscore (`rows_`);
- functions `camelCase`;
- constants `kPascalCase`;
- concepts `snake_case`, as the standard names its own - an adjective or a noun, with no `is_` or `has_`
  (`std::integral`, `std::ranges::input_range`; `column`, `primary_keyed`): `is_` is a trait's (`std::is_integral_v`);
- macros `SCREAMING_SNAKE_CASE`, prefixed `DREAMOR_` (a bare `CHECK` collides with Abseil and glog, see
  `tests/support/check.hpp`).

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

Scripts (`tools/`, `ci/`, `tests/`'s helpers, `bench/`'s harness): a short one that chains commands is shell; past 100
lines, or one that handles data - lists of cases, results to compare, JSON - consider Python, the standard library
alone, 3.12 or later (the oldest the CI's machines have). A recommendation, not a rule.

Wrap third-party includes (Concert, OR-Tools, HiGHS) in `DREAMOR_BEGIN_THIRD_PARTY_HEADERS` /
`DREAMOR_END_THIRD_PARTY_HEADERS` from `internal/base/third_party_headers.hpp`. That way `-Wall -Wextra` (or `-Werror`)
reports only this library's code.

### A header's order

A header has one owner - a public class, or a family of functions used together - and the helpers it is built from;
helpers that grow a header of their own move to an internal header (`key_args.hpp`, `binding.hpp` and `plan.hpp` are
such). Its order is libstdc++'s: the parts before what is built from them, which two-phase lookup asks of a template
anyway, each kind in one place:

1. the license lines, the line saying what the header holds, the `#include`s;
2. the `DREAMOR_INTERNAL_..._MESSAGE` macros;
3. forward declarations the helpers need, in blocks of declarations alone (`template <typename T> class Ref;`);
4. the helpers, in one internal namespace (`internal`, or a public header's `detail`), in the order the public part
   uses them;
5. the public part, in one block a public namespace: concepts, classes, each class's non-member functions and
   operators, function objects, deduction guides. An `@hidden` overload, which only stops on a sentence, follows the
   function it guards. The public macros (`DREAMOR_ENTITY_COLUMN`) follow the namespace;
6. the `std` specializations (`std::hash`, `std::formatter`).

A namespace is opened at file scope by its full name - `namespace dreamor::internal::inline index_set {`, never inside
another - so that a block reads, and is checked, line by line. In a class: public, then protected, then private; in
each, types and aliases, constants, constructors and assignment, the other member functions, data members (Google's
C++ style). `tools/check_layers.py` checks the namespaces - each opened once, by its full name, the internal ones
before the public ones; the rest of the order is the review's. A commit that only moves code to this order changes
nothing else, and goes into `.git-blame-ignore-revs`.

### Model code in this repository

The code that shows dreamor in use: the code blocks of the README, the guide's code (`tests/docs/`), `examples/` (in the
ported `mathopt_samples/`, the code that uses dreamor, in Google's format by hand; the rest stays as Google wrote it),
and the benchmarks' dreamor models. Review holds it; `examples/.clang-tidy` turns the library's naming check off.

- **Names.** Columns `cPascalCase`, sets `sSCREAMING_SNAKE`, variable families `vPascalCase`; callables that take
  columns in `snake_case`; everything else in `camelCase`.
- **Named callables.** The model code here names its callables - a local variable, or a `static constexpr` member
  or namespace-scope constant where there is no local scope (a constructor's member-init list) - and passes them by
  name: to `where`, `sumExpr`, a VarTable's bounds, `IndexSet{records, row_of}`. It shows the form recommended for a
  predicate or a term that says something of the model (`fits_batch`, `hours`). Naming is a recommendation, not a
  rule: in a user's model, a lambda holding logic special to one set may stay inline where that set is defined,
  rather than declared ahead of it where it breaks the code's flow (the skill says so).
- **Sets from relations.** A relation is data the builder fills once (a machine's `items`), read through a lateral
  source, not a join that looks a key up again; a projection through `select`. Rows of a set, and functions taking them, use column types (`cMachine`,
  `std::tuple<cMachine, cItem>` for rows gathered before the set exists); an entity field that refers to another
  entity, a nullable find result and a lateral source's range hold `Ref<T>`. Entities are owned by
  `EntityVector<T>`; a subset is a range of `Ref<T>`. A family of rows runs over its domain: the condition that
  decides whether a row is there goes into the set the loop runs over (`S.where(not_first)`), not an `if` around
  the row in the body nor a sentinel value standing for "no row"; an if-then-else inside a row, choosing a term,
  stays.
- **Text and messages.** A text is a value: `std::format`, or a `FormatBuffer` where one is made often and must
  not allocate. What a model has to say goes through dreamor's logger, at its level: `DREAMOR_LOG_INFO`, `_WARNING`,
  `_ERROR`, `_FATAL` (which aborts). A guide page's code writes nothing to `std::cout`; a whole program - the
  README's, `examples/` - writes its own result where it chooses.
- **Plain C++ types** in the code users read - the README's, the docs' and `examples/`: `int` for a
  period or a count, `long` where it grows, `double` for a quantity, as a user writes them.
- **One model per backend.** The examples keep one model per backend side by side
  (`production_planning*.hpp`), though they repeat each other; they are not folded into one template over a
  backend trait. The sets are dreamor's and shared (`production_planning_sets.hpp`); each model owns its solver's
  objects and is built Sets -> VarTables -> Model. Each model is `planning::<backend>::Model`, with
  `using namespace dreamor::<backend>;` inside its own namespace: the model reads without the prefix, and two models
  stay apart where one file includes both (the cross-backend tests) - a using-directive in the shared `planning`
  would make `VarTable` ambiguous there.

## Tools

- **Formatting** is `.clang-format` (clang-format 23.1.3) and `ruff.toml` (ruff 0.17.1), pinned because output differs
  between versions. The scripts run the pinned versions through `uvx`, so all they need is
  [uv](https://docs.astral.sh/uv/); nothing is installed on PATH, and another project's pins do not collide.
  `examples/mathopt_samples/` is left as ported, and the code blocks of the README and `docs/` are formatted by
  hand. The script also adds the SPDX license line where it is missing, and writes a hyphen for every dash in the
  Markdown documents: an em dash or a minus sign becomes `-`, as every page writes it (`tools/dashes.py`).
  CI runs `tools/format.sh --check`. To bump a formatter, change its pin (`CLANG_FORMAT_VERSION` in `tools/format.sh`,
  `required-version` in `ruff.toml`), run `tools/format.sh`, commit only the reformatting, and add that commit to
  `.git-blame-ignore-revs`. Time to bump: with CI's clang-latest job, at each LLVM major (March and September) -
  clang-format and clang-tidy move to that major's newest patch on PyPI, so the newest Clang CI tests and the
  formatter are one major. `tools/clang-format` is the pinned clang-format as one executable, for an editor's
  format-on-save (VS Code: `clang-format.executable`, which the repository's `.vscode` sets).
- **Linting** is `.clang-tidy` (clang-tidy 23.1.3, run through `uvx` as well): broad check families, with
  the few that clash with this codebase turned off and the reason written beside each. Three identities: the library
  (`include/`) is held by the root's checks with no compromise; tests/ relaxes what test code does on purpose; the
  code users read - examples/ and the documentation's code, tests/docs/ - relaxes besides what a model does as a user
  writes it (a plain enum, const members), its two configurations kept alike. tests/stub, the compile_fail cases, the ported samples and bench/ are
  not checked. An exception to a check that stays on is `NOLINT(check): reason` - never a bare NOLINT. A reason
  longer than the line goes in a plain comment above, and the NOLINT line keeps a short one: clang-format breaks a
  comment past the column limit, and a NOLINTNEXTLINE broken in two excuses its own second line instead of the code.
  A finding on a line a NOLINTNEXTLINE cannot reach (a class head under a template's requires) takes
  NOLINTBEGIN/NOLINTEND around the declaration. Review what
  `tools/tidy.sh --fix` writes: applied from several translation units at once, a fix can land twice or garble a
  call, and const goes on the right.
