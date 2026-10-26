# Philosophy

**English** · [한국어](ko/philosophy.md)

dreamor is small on purpose. This page gathers what it holds to - the principles behind every part, the names a
model uses, the rules of use, and what it leaves out on purpose - so that a user knows what to expect of it and a
contributor knows what a change has to keep.

## Design principles

- **Columns are called by name.** You refer to a column by its column type, such as `MachineCol` or `PeriodCol`,
  not by its position in a tuple. Predicates, bound functions, and the terms of a sum are all callables that
  **take the columns they read as parameters**.
- **A set is a fact.** Once built, a set cannot be changed (it is read-only). When you need another set, you derive
  a new one with `where`, `select`, and the like.
- **One VarTable per variable family.** Which variables exist is decided by that table's set. The VarTable
  constructor is the only place variables are created.
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
  objectives, solving, bounds after construction and duals are written in Concert's or MathOpt's own notation, so
  everything you know about your solver still applies and nothing stands between you and it.
- **A mistake stops at compile time, in one sentence.** Misuse - a callable that takes the wrong columns, a sum used
  before `build()`, a lookup with bare values - is rejected by the compiler with one sentence saying what to write
  instead, not with a page of template errors.
- **The standard library is the precedent.** Where the C++ standard library made a similar decision, dreamor
  follows it: an insert that may not insert returns `std::pair<iterator, bool>`, as `std::set::emplace` does; checks
  that only debug builds pay for work as libstdc++'s assertions do; member types are named as containers name them.
- **Speed is measured, not claimed.** Claims about performance rest on designed experiments against hand-written
  code ([benchmarks](../bench/README.md)), and an optimization rests on a guarantee dreamor makes - a contract, the
  structure of generation - not on how a benchmark draws its data.

## Naming

Sets and variable families have a fixed name shape: the AMPL model's name with one prefix in front, so the AMPL
comments and the code match once the prefix is dropped.

| What | Shape | Example | AMPL |
|---|---|---|---|
| Column type | PascalCase + `Col` | `MachineCol`, `PeriodCol` | |
| Set (`SetOf`) | `s` + SCREAMING_SNAKE_CASE | `sPRODUCE`, `sPEAK_PRODUCE` | `PRODUCE` |
| Variable family (`VarTable`) | `v` + PascalCase | `vYield`, `vSetup`, `vStock` | `Yield` |

Sets get a prefix because an all-caps name in C++ looks like a macro or a constant (`ILOFLOAT`, `ILOBOOL`).

Other names (callables such as predicates and terms, expressions, groupings, solution tables) are not prescribed.
The examples in this document write callables that take columns in snake_case (`items_of`, `machine_usage`) and
everything else in camelCase (`totalInFlow`, `yieldPlan`).

## Rules

1. Callables (predicates, dependent sources, bound functions, terms of sums) take **column types** as parameters.
   `[](const Item* i)` or `[](auto i)` is a compile error. The only exception is the key of `orderBy` (which takes
   the whole row).
2. A dependent source returns a range of **pointers** to entities.
3. Sets are read-only. Derive instead of modifying.
4. One VarTable per variable family, over the family's **whole set**. Reach parts with `vYield(row)`.
5. If every key of the driver set needs a constraint, do not iterate the `Grouped`; iterate the driver and call
   `slice()`.
6. No source holds an element twice, and an entity is one object in one container. Sets then hold no row twice.
7. Do not `end()` variables. `env.end()` once, at the end.
8. Use `Grouped`'s `slice()`/`at()`/`keyAt()` and `Ordered`'s lookups on named objects. A group is valid only while
   that object is alive. If you only iterate, a temporary works too, as in `for (... : S.groupBy<C>())`.
9. A function object whose `operator()` is a template, so that its columns cannot be read from its parameters, can
   be used as a predicate by specializing `dreamor::PredicateColumns` to declare the columns it reads.
   ```cpp
   template <> struct dreamor::PredicateColumns<MyFilter> { using type = std::tuple<MachineCol, PeriodCol>; };
   ```
10. In generic code (`[](const auto& sANY)`), calling a member that takes columns as template arguments, such as
    `sANY.select<A>()`, requires `sANY.template select<A>()`. This does not come up in the recommended style, which
    handles sets by their concrete types.
11. Look up by key with column values in declaration order (`vYield(m, i, t)`, `slice(i, t)`, `prev(t)`). Braces and
    bare values are not accepted (see [Columns](guide.md#columns)).

## What dreamor does not do

| Not done | Reason / alternative |
|---|---|
| Constraint tables (storing constraints per key, key-based constraint names) | Most constraints are added once and never looked up again. If needed, use the solver API (Concert `IloRange` + `setName`, MathOpt `AddLinearConstraint(expr, name)`) |
| Reading duals / reduced costs by key | A rare task. Use the solver API when needed (Concert `getDuals`, MathOpt `dual_values()`, etc.) |
| Appending variables (column generation) | Structure is immutable. New columns mean a new set + a new VarTable |
| Pipe syntax (`S \| where(p)`) | No second way to do the same thing |
| Coefficient arrays (`IloScalProd` style) | Coefficients are computed from entities in the term |
| Checking for duplicate variable creation | Covered by rule 4 instead |
| Constraint programming (interval variables, `noOverlap`, cumulative functions) | Outside the scope of an algebraic modeling layer. The set layer is solver-independent, so when writing scheduling with CP Optimizer or CP-SAT you can still pass rows built with `makeSet`, `groupBy`, and `ordered()` straight to that API |

