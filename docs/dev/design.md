# Design rules

For people working on dreamor; `AGENTS.md` says when to read it.

The principles are `docs/philosophy.md`'s, written there once, for users: changes must keep them. They are the
library's reason to exist, not style preferences. This page holds what they decide for a change to dreamor - the
tests a change must pass, the places they fix, and what stays out.

- **The scope is a class of problems, not a field** ("Sets are relations over your data"). A set is not in or out of
  scope because a scheduler or a network uses it. The transitive closure is an operation beside `unite`, not a step
  of the chain. A new operation is added only for a set formulations commonly write and a model would easily get
  wrong by hand; a set beyond the algebra and the closure is handed over as records (`IndexSet{records, row_of}`).
- **A type rule that makes an expression harder to write than its formula is a bug** ("Types identify, and go no
  further"), unless what it stops is a mistake of identification - two kinds compared, a value put in another
  column's place.
- **Columns are named, not positional.** Callables take the column types they read; lookups take key columns in
  declaration order or one row. A bare value goes where its type names one column of the key, and is rejected
  where two columns share its type; a braced list in a key's place stays rejected. A source of one named column
  may be values in braces, `from<cShift>({Shift::kDay, Shift::kNight})`: the column names them.
- **Sets are immutable.** New sets come from derivation, never mutation.
- **A chain has one entrance, `index_set::relational::from`.** A chain starts at the relation with no column and
  one row, where every step gives back what `from` gives (`cross` a range is the range, `join` a set is the set), so
  the first step is always one source on its own; a new step is a member of the chain, never a free function.
  `from` sits in `dreamor::index_set::relational`, named for the notation it starts, as `interval`'s functions sit
  in `dreamor::interval`: written out it says what it builds, a model that writes many brings it in with a
  using-declaration, and argument-dependent lookup does not find it beside a user's own.
- **set_builder is the chain, written as the notation reads.** `index_set::set_builder::of<C...>(in<C...>(source),
  ...)` writes a set as set-builder notation does, and is lowered onto a chain, generator by generator - as C#'s
  query syntax is method calls. It does nothing a chain cannot, and has nothing of its own in the engine: a new source
  or operation goes into the chain and the engine first, and set_builder only says how it is written. Its checks run
  before the chain's, in its own words; a message the two share names both.
- **Sets are sets, by contract** ("A set is a set"). No user-facing check or deduplication (`distinct`,
  `assertUnique`); a debug build checks every source in a chain's `build()` (`diagnostics.md`).
- **One VarTable per variable family.** A family of variables is created only in a `VarTable` constructor, and a
  table's family never changes once it holds one: a table constructed empty takes a family by move assignment, once
  (`table_filled_once`). A single variable is the solver's own (`IloNumVar`, MathOpt's
  `AddVariable`), or `HighsEnv::addVariable` on HiGHS, whose environment collects the model and so owns the call.
- **No new user-facing concept for performance** ("Speed adds nothing to learn"). Internal hashing and set sharing
  are fine; new knobs in the API are not. How a performance change is judged is `benchmark.md`'s, "A performance
  change".
- **No aliases; a layer earns its place.** A name or syntax that only respells another at the same level - a pipe
  operator for a member call, a free function for a member, a second name for one function - is not added: it
  doubles what a reader must know and gives nothing. A notation at another level is a different question: a higher
  one that writes the common case as the formulation reads, as `einsum` stands beside `matmul`; a lower one that
  does what the higher cannot, as a loop stands beside a range pipeline. Each layer says what it gives that the
  other does not, and one that gives nothing is an alias. Different combinations of tools that reach the same
  result are neither: never add a rule that forbids a combination because another gives the same result.
- **Reach for the standard library's precedent.** When a design decision has an analogue in the C++ standard
  library or its implementations, propose that first and name it: `std::set::emplace`'s `pair<iterator, bool>`
  for an insert that may not insert, libstdc++'s assertions for checks that only debug builds pay for,
  `std::ranges::borrowed_range` for a range whose elements outlive the call that returned it.
- **The solver's own API stays the solver's.** Constraints, objectives, solving, duals, bounds after construction,
  and names are not wrapped. Backends take each solver's notation (`IloEnv`/`ILOFLOAT` vs `Model&`/`is_integer`).
  HiGHS's C++ API has no expressions, so its backend's notation is highspy's - `addConstr`, `setObjective`,
  `minimize` ("Backend parity").
- **Out of scope.** Do not add any of these without the maintainer asking:
  - constraint tables;
  - keyed dual or reduced-cost accessors;
  - column generation;
  - coefficient-array overloads;
  - user-facing duplicate checks or deduplication of sets (see "Sets are sets, by contract");
  - a set or its builder owning a source container, to take a temporary `EntityVector` or the strings a text column
    shows;
  - constraint programming (interval variables, `noOverlap`, cumulative functions).

## Backend parity

These behave identically on every backend. A change to one is a change to all:

- bounds as numbers or column callables;
- variable names `<name>_<key0>_<key1>...`;
- `std::out_of_range` for a lookup outside the set;
- iteration in set order;
- `sumExpr`'s `where()` / `whereEach()` / `build()`, linear or quadratic terms (`build()` gives each solver's expression
  that holds them: `IloExpr`, MathOpt's `LinearExpression` / `QuadraticExpression`, or HiGHS's
  `HighsLinearExpression` / `HighsQuadraticExpression`);
- a `VarTable` as a sum term.

Backend-only features stay in their backend:
- Concert and HiGHS, not MathOpt (whose models have none): semi-continuous variables - Concert's `semiContinuous`
  constructor, HiGHS's `kSemiContinuous` and `kSemiInteger` as the type.
- MathOpt and HiGHS: `std::logic_error` from `solution()` when there is no (primal) solution - MathOpt would
  otherwise CHECK-fail, and HiGHS would hand back the values it left from before.
- HiGHS only: `HighsEnv`, the model collected and passed to the `Highs` once, and its `addVariable` for one variable
  (highspy's); `addConstr`, `setObjective`, `minimize` and `maximize` (`minimize`/`maximize` set the objective and
  `run()`, as highspy's do) - HiGHS's C++ API is a matrix API with no expression to write a row or an objective
  in, so this backend brings highspy's declarative interface and its four calls, where Concert and MathOpt hand
  their own; `val(env, x)`, highspy's `val`, for one variable's or a linear expression's value, and a quadratic
  expression's beside it (highspy has none), which HiGHS's column-index API has no call for; a row as a type of its own, so a row given as an objective and a quadratic row stop at compile time;
  `HighsCallError` for a HiGHS call that fails.

Shared logic belongs in `include/dreamor/internal/backend/`.
