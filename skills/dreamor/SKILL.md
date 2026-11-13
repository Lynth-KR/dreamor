---
name: dreamor
description: Write, review, and fix C++ optimization models that use dreamor (entities in EntityVector, Ref, columns of entities and of plain values, index sets, ParamTable, VarTable, sumExpr, SolutionTable) on HiGHS, CPLEX Concert, or OR-Tools MathOpt. Use this skill whenever code includes "dreamor/..." headers or uses dreamor::index_set::relational::from (set chains - cross, lateral, join, where, build), dreamor::index_set::set_builder (of, in), IndexSet, EntityVector, Ref, DREAMOR_ENTITY_COLUMN, DREAMOR_VALUE_COLUMN, groupBy, slice, ordered, orderedWithin, ParamTable, VarTable, SolutionTable, sumExpr, indexedName, or HighsEnv, addConstr and minimize from dreamor::highs; whenever the user wants to write a model (sets, data on them, indexed variables, sums, constraint families) in C++ with dreamor; and whenever a compile error or a debug-build abort mentions dreamor, VarTable, sumExpr, UnbuiltSum, or messages such as "Look a variable or value up by the table's key columns", "is not an expression yet" or "a broken contract".
---

# dreamor (0.9)

dreamor is a header-only C++20 modeling layer: a model's data as entities, the index sets over them, and on those
sets data, variable families and sums, checked by the compiler. Constraints, objectives, solving, options, bounds
after construction and duals are the solver's own API - on HiGHS, `dreamor::highs` brings highspy's `addConstr`,
`setObjective`, `minimize`; on Concert `model.add`, `IloCplex`; on MathOpt `AddLinearConstraint`, `Solve`. dreamor has
no constraint tables, no column generation, no constraint programming, no keyed duals. Do not invent API: every
public name is in the reference below.

## Where to look

This folder carries the documentation as files. Read the page for the task before writing its code, and follow its
code: it is compiled and checked as written.

- `docs/guide/at-a-glance.md` - the names a first model needs, and the ones to learn next. Prefer those names.
- `docs/guide/index.md` - a model's parts and where each is shown. Then the page for the task:

  | To | Read |
  |---|---|
  | hold the data's entities, refer to one | `docs/guide/entities.md` |
  | declare columns; write a callable or a key | `docs/guide/columns.md` |
  | build a set | `docs/guide/relational-algebra.md` (a chain), `set-builder-notation.md` (`of`, `in`), `sets-from-records.md` |
  | make a set from another, or two | `docs/guide/deriving-sets.md`, `set-algebra.md` |
  | group rows; read a neighbour, the period before | `docs/guide/grouping.md`, `ordered-sets.md` |
  | compute a value over rows before a solve; hold data on a set | `docs/guide/values-over-a-set.md`, `data-on-a-set.md` |
  | make variables; sum them; add rows and the objective | `docs/guide/variable-families.md`, `sums.md`, `constraints-and-objective.md` |
  | read the solution; carry it into the next model | `docs/guide/solutions.md` |
  | name rows; log | `docs/guide/names-and-logging.md` |
  | HiGHS's environment and its own API; Concert's and MathOpt's objects | `docs/guide/backends.md` |

- `docs/reference/` - every public name, its signature, what it takes, throws and costs, a page a topic
  (`index.md`); `debug-build-checks.md`, every contract; `exceptions.md`, what throws what. Read the entry before
  using a member the guide does not show.
- `docs/reference/compile-time-messages.md` - every compile-time message, a heading each in the compiler's words,
  with when it comes and what to write instead on the two lines below it: search it for the words of the first error
  whenever a build stops in dreamor code. A debug run that aborts names its contract in `debug-build-checks.md`, and a
  throw is in `exceptions.md`.

## Writing a model

In this order - each step takes what the one before made:

1. **Entities**: each kind in a `dreamor::EntityVector<T>`, by its `primaryKey()`, filled, then `freeze()`; a field
   that refers to another entity is a `dreamor::Ref<T>`, taken with `at` after that vector is frozen.
2. **Columns**: `DREAMOR_ENTITY_COLUMN(cMachine, Machine)`, `DREAMOR_VALUE_COLUMN(cPeriod, int)`.
3. **Sets**: a chain from `dreamor::index_set::relational::from`, or set-builder notation; then derived ones.
4. **Data on sets** no entity holds: `dreamor::ParamTable`.
5. **Variables**: a backend's `VarTable` over each family's whole set.
6. **Rows**: a loop over the set a family runs over, a backend's `sumExpr(...).build()` in it, the solver's own call.
7. **Solve, then read**: `solution()` gives a `SolutionTable`.

`VarTable` and `sumExpr` are a backend's (`dreamor::highs`, `concert`, `mathopt`): there is no `dreamor::VarTable`. A
backend header brings the set layer with it. CMake: `find_package(dreamor 0.9 CONFIG REQUIRED)`, link
`dreamor::dreamor` and the solver. Build without `NDEBUG` while writing a model: the debug build checks the contracts.

## What agents get wrong

Each is a rule of the reference or the guide, linked; the compile-time ones stop with a sentence that
`docs/reference/compile-time-messages.md` explains.

- **A callable takes the columns it reads**, by value or `const&`, one non-template `operator() const`: no `auto`
  parameter, no `mutable`, no `std::function`, no `Ref<T>` or `int` parameter where a column goes. A generic lambda
  goes through `dreamor::reads<cMachine, cPeriod>(f)`, which names the columns it is called with
  (`docs/reference/column-callables.md`, `docs/guide/columns.md`).
- **A row is a `std::tuple` of its columns**: name them all with a structured binding, or take one with
  `std::get<cItem>(row)` (`docs/guide/columns.md`).
- **A lookup takes the key's columns in the table's order, or one row**; where two key columns hold one type
  (`cParent`, `cChild`), name each, `quantity(cParent{a}, cChild{b})`; no braced list (`docs/reference/keys.md`).
- **A column holds a `Ref` or a plain value** - never floating point (a quantity is an entity's field or a
  `ParamTable`), never a pointer (`docs/reference/columns.md`).
- **An `EntityVector` is filled, frozen, then read**; `find` gives an empty `Ref` when nothing has the key - check it
  before reading (`docs/reference/entities.md`).
- **No source holds an element twice**, and what a row refers to outlives the set: a named, frozen `EntityVector`, a
  container of the text a `std::string_view` column shows (`docs/reference/debug-build-checks.md`).
- **A family of rows runs over its domain**: a condition on a row's keys is a `where` on the set the loop runs over,
  not an `if` around the row; an `if` inside a row chooses a term. A family over every key loops over the key's set
  and `slice`s a named `Grouped` - an empty slice sums to 0 (`docs/guide/ordered-sets.md`, `grouping.md`). A part
  of the model left out builds its set as `IndexSet<...>::none()`, and its families, rows and sums are empty with it
  (`docs/guide/variable-families.md`).
- **One `VarTable` a family**, its keys its set's columns in the same order; reach a subset's variables through it.
  Tables are built after their sets: in the initializer list, or declared empty and a family moved in once
  (`docs/guide/variable-families.md`). A `VarTable`'s type may be left out, `VarTable vMake{env, sPRODUCE, ...}`, and so
  may a `ParamTable`'s on a set, `ParamTable demand{sPLAN, demand_of}` - its value type is the argument's own (`0` is
  `int`, a string literal `std::string_view`, a `const char*` or a `char` buffer `std::string`); name it for another
  (`docs/guide/data-on-a-set.md`).
- **`sumExpr(...)` is a builder until `.build()`**; on Concert it takes the environment first,
  `sumExpr(env, rows, term)` (`docs/reference/sums.md`).
- **Name a `Grouped` or an `Ordered`** before `slice`, `at`, `next`, `prev`, `first` (`docs/reference/grouped.md`,
  `ordered.md`). Both are public types a class can hold, declared empty and assigned later
  (`docs/guide/ordered-sets.md`).
- **A set's order is the order its rows were made in**: read neighbours on a set whose order you made or sorted
  (`orderBy`).
- **HiGHS**: build the model before `env.highs()` is first reached; give options to `HighsEnv{options}`; one
  environment's variables in an expression; a failing HiGHS call throws `dreamor::highs::HighsCallError`
  (`docs/guide/backends.md`).
- **Name rows** with `dreamor::indexedName("Balance", i, t)`, the rule `VarTable` names its variables by. Text is a
  value (`std::format`); what a model has to say goes through `DREAMOR_LOG_INFO`, `_WARNING`, `_ERROR`
  (`docs/guide/names-and-logging.md`).
- **Name a callable that says something of the model** or that another set, sum or constraint family could take
  (`fits_batch`, `hours`), and pass it by name. Naming is a recommendation, not a rule: a lambda holding logic
  special to one set - a projection that builds that set's rows, a condition no other set asks - stays inline where
  the set is defined, rather than declared ahead of it where it breaks the code's flow. Where the user's code shows
  no style of its own, name as dreamor's documentation does: columns `cMachine`, sets `sMAKE`, variable families
  `vMake`, callables over columns `fits_batch`, everything else `camelCase`; a set's base in capitals and what narrows
  it in lower case after, `sMAKE_peak`; a member of a struct that holds one kind drops the prefix and keeps the case,
  `sets.MAKE`, `vars.Make` (`docs/philosophy.md`).

## Before finishing

- [ ] The code follows the guide page for each part it writes; every member used is in `docs/reference/`.
- [ ] Every callable takes columns, with no `auto` parameter - a generic one wrapped in `dreamor::reads<...>`; a
      predicate or term that says something of the model is named, logic special to one set may stay inline.
- [ ] Every lookup passes the key in the table's order, or one row; where two key columns share a type, each is named.
- [ ] Entities live in named, frozen `EntityVector`s; fields that refer to another entity are `Ref<T>`; an empty `Ref`
      from `find` is checked.
- [ ] Each `VarTable` and `ParamTable` sits on a set with exactly its key columns, in order; one `VarTable` a family.
- [ ] Every `sumExpr` ends in `.build()` and has the backend's arguments.
- [ ] Each constraint family loops over its domain: `where` on the set for a row only some keys have, `slice` for a
      family over every key; `if` only to choose a term inside a row.
- [ ] Rows are named with `indexedName`; the solver's own calls make constraints, the objective, the solve.
- [ ] The model builds without `NDEBUG` and runs once there: the debug build checks the contracts it can.
