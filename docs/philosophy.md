# Philosophy

dreamor is small on purpose. This page explains what it holds to and why, so that you know what to expect from it -
where it will stop you, what it will trust you with, and what it leaves to you. The [guide](guide/index.md) shows how
to do each thing; this page is about why things are the way they are.

## What dreamor is

An optimization model is mostly sets. Which machines can make which items, in which periods, through which routes:
before a single variable exists, a model decides over which rows each family of variables, each sum and each
constraint runs. dreamor treats those sets as what they are - relations over your data - and lets you write them
the way you think of them: from your entities, joined on what they share, narrowed by conditions, grouped and
ordered.

Relations are what SQL is built on, and dreamor is close to it in spirit: a set is a table of rows, a column has a
name and a type, and sets combine by the relational algebra. What is new is where the rows go next. A set's rows are
the keys of your variables, the terms of your sums and the rows of your constraints, and the solver you already use
takes it from there.

## What it aims for

- **Small.** A header-only C++20 library with a handful of ideas: entities, columns, sets, tables of variables and
  data, and sums. Each idea has one job, and you can hold all of them in your head.
- **Fast.** Building a model's sets and variables should cost about what the same model written by hand costs, so
  that you never have to choose between clarity and speed. This is measured, not claimed: the
  [benchmarks](https://github.com/Lynth-KR/dreamor/tree/main/bench) compare dreamor with the same work written by hand,
  each operation over the shapes data takes and whole models up to the solver's start, and anyone can run them
  again.
- **Predictable.** A mistake dreamor can see from your types stops your build, with one sentence saying what to write
  instead. What it cannot see is a contract, written down once and checked by a debug build. Nothing is guessed or
  silently fixed behind your back.

## The principles

These shape every part of dreamor. Each comes with what it means for the code you write.

### Sets are relations over your data

dreamor writes the index sets of LP, QP, QCP and MIP models. If such a formulation writes a set, dreamor should be
able to write it; one it cannot is a missing feature, whatever field your model comes from. The tools are the
relational algebra - products, joins, conditions, projections, unions and differences - and the transitive closure
of a relation, which the algebra alone cannot express (Aho and Ullman, 1979): every part below a product in a bill of
materials, every node a network reaches.

A set beyond those - a list of paths, a fixpoint of your own - is computed in C++, where your model already lives, and
handed to dreamor as records. You never have to bend an algorithm to fit a library.

### Columns have names

A column is a type of its own, such as `cMachine` or `cPeriod`, and you refer to it by that name, never by its
position. A condition, a bound or the term of a sum is a function that takes the columns it reads:

```cpp
const auto fits = [](cMachine m, cItem i) { return i->size <= m->capacity; };
```

So the code says which data it uses, and reordering a set's columns never silently changes what a function reads.

### Types identify, and go no further

dreamor's types tell which entity, which kind of value and which column - nothing more. Your expressions read as
your formula does: `t >= m->opens`, `t + 1`, `cost * vMake(m, i, t)`. If a type rule ever makes an expression harder
to write than its formula, that is a bug in dreamor, unless what it stops is a real mix-up, such as two kinds of
entity compared or a value put in another column's place.

### Each kind of data has one home

- An entity's own attributes live on the entity, a plain struct of yours in an `EntityVector`. A set's rows hold the
  entities themselves, so `m->capacity` is right there with the key.
- A relation between entities is a set, an `IndexSet`.
- A value on each row of a set is a `ParamTable` when it is your input, and a `SolutionTable` when a solve produced
  it. A solve's results carry into the next model as a `ParamTable` built from them.

### A set is a fact

Once built, a set's rows never change. Every other set is derived from it - by a condition, a projection, a join, the
set algebra - and the original stays as it was. A variable family is built once over its set, too. A name may be
assigned another set as a whole, as any C++ value may, but nothing changes the rows that a set holds: whatever holds
a set, or a table built on it, reads the same rows to the end. This is what makes a model easy to reason about.

### A set is a set

A set never holds a row twice, so a table of variables has exactly one variable per key. dreamor's own operations
never make a repeat; what it asks of you is that a container you hand it does not hold an element twice, and that
each entity is one object in one container. As in the standard library, this is a precondition: a debug build
checks every source and stops with a sentence naming it, and a release build does not pay for the check.

### A set holds rows, not your containers

A row copies its values and its references to entities (`Ref`). What those references point to - your entities in
their `EntityVector`, the characters a `std::string_view` column shows - lives in your containers and has to outlive
the set, as the elements a `std::span` views outlive the span. Where the types can tell that a container would go
away too soon, your build stops; where they cannot, the rule is the same one C++ already has for views.

### Your data, as you prepared it

dreamor takes your data as it is, and trusts that it meets the contracts its documentation writes down. It does not
clean, repair or second-guess your data, and its messages say which contract broke, never how your data ought to be
prepared - that part is yours, and you know it best. The contracts are listed in one place,
[Debug-build checks](reference/debug-build-checks.md).

### The solver stays yours

dreamor builds sets, variable families and sums. Constraints, objectives, solving, bounds after construction and
duals are written in your solver's own notation, so everything you know about your solver still applies and nothing
stands between you and it. On CPLEX that is Concert, on OR-Tools it is MathOpt. HiGHS's C++ interface has no
expressions to write a row in, so dreamor brings the few calls highspy has for that - `addConstr`, `setObjective`,
`minimize`, `maximize` - and leaves the rest to HiGHS.

### Speed adds nothing to learn

dreamor's internals hash, share and lay out memory as they need to, but none of it reaches the API: there is no
option to tune and no second way to build a set for speed. The fast way is the only way.

### The standard library goes first

Where the C++ standard library made a similar decision, dreamor follows it, so that what you already know carries
over: an insert that may not insert says whether it did, as `std::set::emplace` does; a frozen `EntityVector`'s
elements stay where they are, as a `std::vector`'s do until it grows; checks that only debug builds pay for work as
the standard library's own assertions do.

## Two ways to write a set

You can write a set as a chain of steps, each saying how a source relates to the rows so far, much as SQL's `JOIN ...
ON` does; or in set-builder notation, close to how a set is first drafted on paper. Both build the same sets, and
neither is the recommended one: which reads better is yours to decide, set by set. The
[README](https://github.com/Lynth-KR/dreamor#quick-start) writes one of each, and the guide's pages on
[relational algebra](guide/relational-algebra.md) and [set-builder notation](guide/set-builder-notation.md) show
them side by side.

dreamor does not add a second spelling of what one notation already writes - a pipe operator for a member call, a
free function for a member. A second notation earns its place only by reading at another level.

## Names in these pages

dreamor does not check your names; a model's names are its author's. The documentation writes columns, sets and
variable families in one shape, and you are welcome to borrow it, because the prefix tells the three apart at a
glance:

| What | Shape | Example |
|---|---|---|
| Column | `c` + PascalCase | `cMachine`, `cPeriod` |
| Set (`IndexSet`) | `s` + SCREAMING_SNAKE_CASE | `sPRODUCE`, `sPEAK_PRODUCE` |
| Variable family (`VarTable`) | `v` + PascalCase | `vYield`, `vStock` |

A set's name gets a prefix because an all-caps name alone reads as a macro or a constant in C++.

## What dreamor leaves to you

Some things are left out on purpose, because your solver, C++ or a model of your own already does them well.

| Left out | What to do instead |
|---|---|
| Tables of constraints | Most constraints are added once and never looked up again. Name a row by its keys with `indexedName`, as a `VarTable` names its variables, and add it through your solver |
| Duals and reduced costs by key | Read them from your solver (Concert's `getDuals`, MathOpt's `dual_values()`, HiGHS's `getSolution().row_dual`) |
| Adding variables to a family (column generation) | A family is built once over its set; new columns are a new set and a new `VarTable` |
| Arrays of coefficients | A sum's term computes its coefficient from the row's entities |
| Removing repeats from your containers | A set never holds a row twice, and your sources are trusted not to ("A set is a set") |
| Constraint programming (interval variables, `noOverlap`, cumulative functions) | The set layer does not depend on a solver, so a CP model can still take its rows from a chain, `groupBy` and `ordered()` |
| Teaching C++, a solver or how to formulate | These pages show dreamor, and trust that you bring the rest |
