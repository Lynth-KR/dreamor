# Philosophy


dreamor is small on purpose. This page gathers what it holds to - the principles behind every part, the names it
recommends, the rules of use, and what it leaves out on purpose - so that a user knows what to expect of it and a
contributor knows what a change has to keep.

## Design principles

- **Columns are called by name.** You refer to a column by its column type, such as `cMachine` or `cPeriod`,
  not by its position in a tuple. Predicates, bound functions, and the terms of a sum are all callables that
  **take the columns they read as parameters**.
- **A set is a fact.** Once built, a set cannot be changed (it is read-only). When you need another set, you derive
  a new one with `where`, `select`, and the like.
- **A family of rows runs over its set.** Which rows a constraint family has is decided by the set its loop runs
  over, as which variables exist is decided by a VarTable's set. A condition on a row's keys is a `where` on that
  set - AMPL's `{(i,t) in STOCK: ord(t) > 1}` - not an `if` around the row in the loop's body. An if-then-else
  inside a row, choosing a term, is AMPL's own idiom and is written as one.
- **One VarTable per variable family.** Which variables exist is decided by that table's set. The VarTable
  constructor is the only place variables are created.
- **Parameters live on the entities.** A set's rows hold the entities themselves as keys, so a parameter defined as
  a field of its entity (an array of structs) comes with the key for free: `m->capacity`, not a table looked up by
  `m`. A value that belongs to a relation makes that relation an entity, and the value its field. A value defined by
  an expression over each row is a named callable that takes the columns and computes it - AMPL's `param` defined by
  an expression: `max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; }` is
  `param max_qty {(m,i) in ROUTES} = cap[m] / time[i]`. A `ValueTable` can hold data on a set, AMPL's `param {S}`,
  and nothing forbids it; but unless a design has a reason to, dreamor recommends the two above, and keeps
  `ValueTable` for what a solve produces: solution values, and what an algorithm computes from them between solves.
- **Clarity is not traded for performance.** Performance work is done only where it adds no concept to the user
  API (internal hashing, set sharing, and so on).
- **One tool for each job.** Each tool has one job, and no two tools share one - which is also why there is no
  second syntax for the same thing (pipe operators, free-function aliases, and so on). Combining tools is never
  restricted, though. When tools are orthogonal, different combinations reaching the same result is natural, and
  either way is right: `makeSet<>(sORDER_ITEMS, sPROMO)` and `sORDER_ITEMS.where(sPROMO.membership())` give the
  same rows.
- **A set is a set.** No source handed to `makeSet` holds an element twice, and an entity is one object in one
  container. dreamor's own operations never make a repeat, so a set never holds a row twice and a `VarTable` has
  exactly one variable per key. This is a precondition, as in the standard library: a debug build checks it and
  stops on a sentence naming the source; a release build does not pay for the check.
- **The solver's API stays the solver's.** dreamor builds index sets, variable families and sums. Constraints,
  objectives, solving, bounds after construction and duals are written in the notation of the solver API each
  backend stands on - Concert's, MathOpt's, highspy's for HiGHS - so everything you know about your solver still
  applies and nothing stands between you and it.
- **A mistake stops at compile time, in one sentence.** Misuse - a callable that takes the wrong columns, a sum used
  before `build()`, a lookup with bare values - is rejected by the compiler with one sentence saying what to write
  instead, not with a page of template errors.
- **The standard library is the precedent.** Where the C++ standard library made a similar decision, dreamor
  follows it: an insert that may not insert returns the element and whether it went in, as `std::set::emplace` does
  (by its address in an `EntityVector`: `std::pair<const T*, bool>`), lasting as a `std::vector`'s element does; checks
  that only debug builds pay for work as libstdc++'s assertions do; member types are named as containers name them.
- **Speed is measured, not claimed.** Claims about performance rest on designed experiments against hand-written
  code ([benchmarks](../bench/README.md)), and an optimization rests on a guarantee dreamor makes - a contract, the
  structure of generation - not on how a benchmark draws its data.

## Naming

dreamor does not check names: a model's names are its author's. The documentation, the examples and the skill write
sets and variable families in one shape - the AMPL model's name with one prefix in front - and recommend it, because
the AMPL comments and the code then match once the prefix is dropped.

| What | Shape | Example | AMPL |
|---|---|---|---|
| Column type | `c` + PascalCase | `cMachine`, `cPeriod` | |
| Set (`SetOf`) | `s` + SCREAMING_SNAKE_CASE | `sPRODUCE`, `sPEAK_PRODUCE` | `PRODUCE` |
| Variable family (`VarTable`) | `v` + PascalCase | `vYield`, `vSetup`, `vStock` | `Yield` |

Sets get a prefix because an all-caps name in C++ looks like a macro or a constant (`ILOFLOAT`, `ILOBOOL`).

The examples in this document also write callables that take columns in snake_case (`items_of`, `machine_usage`)
and everything else in camelCase (`totalInFlow`, `yieldPlan`); that is only their habit, not a recommendation.

## Rules

Three kinds: what the compiler stops, what dreamor takes on trust, and what it recommends.

### Forbidden: a compile error

1. Callables (predicates, dependent sources, bound functions, terms of sums) take **column types** as parameters.
   `[](const Item* i)` or `[](auto i)` is a compile error - the key of `orderBy` included. A function object whose
   `operator()` is a template, so that its columns cannot be read from its parameters, can still be a predicate:
   specialize `dreamor::PredicateColumns` to declare the columns it reads.
   ```cpp
   template <> struct dreamor::PredicateColumns<MyFilter> { using type = std::tuple<cMachine, cPeriod>; };
   ```
2. A dependent source returns a range of **pointers** to entities.
3. Sets are read-only. Derive instead of modifying.
4. Look up by key with column values in declaration order (`vYield(m, i, t)`, `slice(i, t)`, `prev(t)`). Braces and
   bare values are not accepted (see [Columns](guide.md#columns)).
5. `Grouped`'s `slice()`/`at()`/`keyAt()` and `Ordered`'s lookups are called on a named object, not on a temporary.
   Only iterating needs no name: `for (... : S.groupBy<C>())`.

### Contract: undefined behavior if broken

6. No source holds an element twice, and an entity is one object in one container. Sets then hold no row twice. A
   debug build checks it and stops on a sentence naming the source.
7. A group, a slice or a row is valid only while the object it came from is alive.
8. Concert: do not `end()` variables, and `env.end()` once, at the end.

### Recommended

9. One VarTable per variable family, over the family's **whole set**. Reach parts with `vYield(row)`.
10. If every key of the driver set needs a constraint, do not iterate the `Grouped`; iterate the driver and call
    `slice()`.
11. Handle sets by their concrete types. In generic code (`[](const auto& sANY)`), calling a member that takes
    columns as template arguments, such as `sANY.select<A>()`, requires `sANY.template select<A>()`.

## What dreamor does not do

| Not done | Reason / alternative |
|---|---|
| Constraint tables (storing constraints per key) | Most constraints are added once and never looked up again. A row is named by its keys with `exprName`, as `VarTable` names variables, and added through the solver API (Concert `IloRange` + `setName`, MathOpt `AddLinearConstraint(expr, name)`, HiGHS `addConstr(env, row, name)`) |
| Reading duals / reduced costs by key | A rare task. Use the solver API when needed (Concert `getDuals`, MathOpt `dual_values()`, HiGHS `getSolution().row_dual`, etc.) |
| Appending variables (column generation) | Structure is immutable. New columns mean a new set + a new VarTable |
| Pipe syntax (`S \| where(p)`) | No second way to do the same thing |
| Coefficient arrays (`IloScalProd` style) | Coefficients are computed from entities in the term |
| Checking for duplicate variable creation | Covered by rules 6 and 9 instead |
| Constraint programming (interval variables, `noOverlap`, cumulative functions) | Outside the scope of an algebraic modeling layer. The set layer is solver-independent, so when writing scheduling with CP Optimizer or CP-SAT you can still pass rows built with `makeSet`, `groupBy`, and `ordered()` straight to that API |

