# Scenario design

The two models, their phases and arms, what the arms are for, and how every arm is checked to build the same
model. `index.md` says what the scenarios are, `running.md` how to run them, and `results.md` what the reference run
found.

## p-median

p-median is the model of the modeling-language benchmarks: Pyomo (Hart et al., MPC 2011), JuMP (Lubin & Dunning,
IJOC 2015, doi:10.1287/ijoc.2014.0623) and MathOptInterface (Legat et al., IJOC 2022, doi:10.1287/ijoc.2021.1067).
The scenario runs it at their sizes. C = 100 customers each sit at one of L candidate locations, drawn at random, and
P = 100 of the locations are opened. L is 5000, 10000, 20000 or 50000. Each repetition draws its own instance, from
the run's seed plus the repetition's number.

```
min  sum {l, c} |at(c) - l| x[l,c]
s.t. sum {l} x[l,c] = 1      for every customer c       (assign)
     x[l,c] <= y[l]          for every location l and c  (link)
     sum {l} y[l] = P                                    (budget)
     0 <= x <= 1, y binary
```

The model has L x C variables x and as many link constraints: 5e5 to 5e6.

### Phases

Every arm times the same phases, in this order:

- `sets`: dreamor's sets. The hand-written arms time this phase empty, and the report shows dreamor's time for it
  instead of a ratio.
- `vars`: the variables y and x.
- `objective`: the objective, handed to the model.
- `assign`: one constraint for each customer.
- `link`: one constraint for each location and customer.
- `budget`: the one constraint over y.
- `extract`: the model, handed to the solver. This is `IloCplex::extract` on Concert, `NewIncrementalSolver` on
  MathOpt, and `passModel` on HiGHS. The `callable` arm times its one `CPXnewcols` and one `CPXaddrows` here. An arm
  that hands the solver one row at a time times this phase empty, because the solver already holds the model.
- `solve0`: a solve with a time limit of zero. The modeling-language benchmarks stop the clock here.

Each run records both `extract` and `solve0`. Their sum with the model phases is the time to the solver's start. Each solver's API splits the work between the two in its own way. The stub has no solver, so a run against
the stub stops after `budget`.

### Arms

- `dreamor` writes the model as the benchmarks' dreamor code does: sets in set-builder notation, `VarTable`s, and
  sums with `sumExpr`. The assign constraints iterate the customers and take each customer's `groupBy` slice. The link
  constraints walk the set and look x and y up by the row's columns.
- `hand` writes the model straight against the solver's API, as the benchmarks' C++ baselines do. The variables are
  in `std::vector`s indexed by position (`x[l * C + c]`), and the sums are loops. HiGHS's C++ API takes a model as
  arrays, so on HiGHS this arm fills the arrays of one `HighsModel` by position and hands them over in one
  `passModel`, in `extract`.
- `calls` runs on HiGHS only. It writes the same rows with one `addRow` call for each row, as code written against
  HiGHS's API one constraint at a time does. The columns go in two `addVars` calls.
- `callable` and `callable-rows` run on CPLEX only. They use CPLEX's Callable Library, the C API under Concert, as
  CPLEX's own examples write a model. `callable` fills the arrays by position and hands them over in one `CPXnewcols`
  and one `CPXaddrows`, in `extract`. `callable-rows` adds the columns in two `CPXnewcols` calls, the costs in one
  `CPXchgobj`, and then each row in a `CPXaddrows` of its own.

On Concert, both the `dreamor` and the `hand` arm add one constraint at a time and end each expression once the model
holds it, as Concert code does. On MathOpt, both arms add one constraint at a time, and the hand-written arm moves its
expressions into the constraints.

## Lot sizing

This scenario builds the repository's own example (`examples/production_planning*.hpp`), with its model and its data
unchanged. The data is the same in every repetition, from the example's seed 7. M = 20 machines each route about 60%
of I items, over T = 50 periods, which makes about 600 production rows for each item. I = 170 gives about 1e5 rows,
and I = 1700 about 1e6.

The model has these variables and constraints:

- Yield and Setup on every (machine, item, period) of the routes, and Stock on every (item, period).
- A stock balance for each (item, period), a capacity for each (machine, period), and a setup link for each
  production row.
- An objective that sums the costs of holding stock and of the setups.

Every variable is named, as the example names them. p-median is dense and indexed by ranges. This model is sparse and
keyed: its rows are generated from the routes, and its sums are groups and slices.

### Phases

- `variables`: the entities and the sets, or whatever an arm keeps in their place, and the three families of
  variables. For dreamor, that includes reading the records into `EntityVector`s.
- `constraints`: every constraint and the objective.
- `extract` and `solve0`, as in p-median. The stub has no solver, so a run against the stub stops after
  `constraints`.

### Arms

- `dreamor` is the example itself: `planning::concert::Model` on Concert, `planning::mathopt::Model` on MathOpt and
  `planning::highs::Model` on HiGHS.
- `flat` is hand-optimized code. It keeps the routes in machine order and each item's routes in a CSR. It keeps the
  variables in `std::vector`s by position, route x T + period, and writes the names with `std::to_chars`. It makes no
  production rows, because it knows that they are the routes times the periods. On HiGHS, it fills the arrays of one
  `HighsModel`, names included, and hands them over in one `passModel`.
- `plain` is `planning::PlainModel`, the example's own comparison. It keeps `std::map`s keyed by the rows' numbers and loops
  over the data, the way the model is usually written without index sets. As in the example, it runs on Concert only,
  which includes the stub. It runs up to I = 1700 only: its stock balance scans every route for each (item, period),
  so its time grows with the square of the items.
- `callable` and `callable-rows` run on CPLEX only. They build flat's model through the Callable Library, as in
  p-median. `callable-rows` adds the named columns in one `CPXnewcols`, then each row in a `CPXaddrows` of its own,
  then the costs in one `CPXchgobj`.

## What the arms are for

dreamor is an expression API. The API it stands against is the expression API that a backend's users write in:
Concert on CPLEX, and MathOpt on HiGHS. `dreamor::highs` writes HiGHS's own API in highspy's terms, and its rival is
MathOpt too.

The matrix arms, `callable` on CPLEX and, on HiGHS, p-median's `hand` and lot sizing's `flat`, are a lower bound, not
a rival. They are C-style code that fills the solver's arrays by position. Hardly anyone writes such code by hand, in
the same way that few write the Callable Library although it is there. `report.py` sets every arm against its
backend's matrix arm, so that what each expression API costs over the arrays stands side by side: Concert's over
CPLEX's own Callable Library, the vendor's two APIs, and `dreamor::highs`'s over HiGHS's arrays.

Every hand-written arm is written for its one model alone, without dreamor's checks, exception safety or generality,
as the benchmarks' hand-written arms are ([Benchmarks](../index.md)): where dreamor matches one, its guarantees cost
nothing there.

## Transparent huge pages

Transparent huge pages are left off, which is the default. The guide's section
[Large models](../../guide/cost.md#large-models) says how to turn them on. With them on, every arm spends far
less time in page faults. On p-median at L = 20000, the time up to the solver's start fell by a third to nearly two
thirds, and the ratios between the arms moved.

## Checks

Every arm must hand the solver the same model. Two checks make sure of it:

- **Each run** checks the rows, the columns and the nonzeros, as the solver counts them. The stub has no solver, so on
  the stub a run checks what the model holds: its rows and nonzeros, the sum of the objective's coefficients, and the
  sum of the constraints' finite bounds. The report names each backend, arm and size whose checks differ from dreamor's.
- **`equivalence.py`** checks the models themselves, before you measure. At each scenario's smallest size, every arm
  on each solver writes the model that the solver holds, outside every timer. `bench/common/compare_mps.py` then
  compares each model with dreamor's by meaning: the bounds, integrality, costs, right-hand sides and coefficients
  must agree within 1e-12, relative to the larger of the two values, or absolute below 1. The names that a solver makes up for what a model left unnamed are compared by
  what they hold. MathOpt writes six significant digits, so on MathOpt the models agree to six digits. If a model
  differs, the script exits with status 1.
