# Scenarios

Layer 2 of the benchmarks: whole models built end to end, once with dreamor and once by hand, up to
the moment a solver starts on them. The catalog (`../catalog/`) measures one operation at a time
over data shapes; a scenario measures what those operations cost together in a model, cache effects
between them included, and through the real solvers. A slow scenario points back to the catalog
family that explains it.

## p-median

The model of the modeling-language benchmarks - Pyomo (Hart et al., MPC 2011), JuMP (Lubin &
Dunning, IJOC 2015, doi:10.1287/ijoc.2014.0623), MathOptInterface (Legat et al., IJOC 2022,
doi:10.1287/ijoc.2021.1067) - at their sizes: C = 100 customers, each at one of L candidate
locations drawn at random, P = 100 locations to open, L = 5000, 10000, 20000, 50000.

```
min  sum {l, c} |at(c) - l| x[l,c]
s.t. sum {l} x[l,c] = 1      for every customer c       (assign)
     x[l,c] <= y[l]          for every location l and c  (link)
     sum {l} y[l] = P                                    (budget)
     0 <= x <= 1, y binary
```

L x C variables x and as many link constraints: 5e5 to 5e6.

**Phases**, the same in each arm: `sets` (dreamor's; the hand-written arm has none), `vars`,
`objective`, `assign`, `link`, `budget`, then `extract` (the model handed to the solver:
`IloCplex::extract`, MathOpt's `NewIncrementalSolver`) and `solve0` (a solve with a time limit of
zero, where the benchmarks stop the clock). Against the stub there is no solver, so a run stops after
`budget`.

**Arms.**
- `dreamor`: sets with `makeSet`, `VarTable`s, the sums with `sumOf`, written as the README writes a
  model - the assign constraints iterate the customers and take each one's `groupBy` slice, the link
  constraints walk the set and look x and y up by the row's columns.
- `hand`: the model written straight against the solver's API, as the benchmarks' C++ baselines
  are - variables in `std::vector`s indexed by position (`x[l * C + c]`), the sums as loops.

Both arms add a constraint at a time and end each expression once it is in the model, as Concert
code does; in MathOpt the hand-written arm moves its expressions into the constraints.

## Lot sizing

The repository's own example (`examples/production_planning*.hpp`), its model and its data as they
are: M = 20 machines, each routing about 60% of I items, over T = 50 periods - about 600 production
rows an item; I = 170 and 1700 give 1e5 and 1e6 rows. Yield and Setup on every (machine, item,
period) of the routes, Stock on every (item, period); a stock balance an (item, period), a capacity a
(machine, period), a setup link a production row, the costs of holding and of setups. Every variable
is named, as the example names them. Where p-median is dense and indexed by ranges, this model is
sparse and keyed: its rows are generated from the routes, its sums are groups and slices.

**Phases**: `variables` (the sets, or whatever an arm keeps instead, and the three families),
`constraints` (every constraint and the objective), then `extract` and `solve0` as above.

**Arms.**
- `dreamor`: `planning::Model` (`planning::MathOptModel` on MathOpt), the example itself.
- `flat`: hand-optimized - the routes in machine order, each item's routes as CSR, the variables in
  `std::vector`s by position (route x T + period), names written with `std::to_chars`. It makes no
  production rows: it knows they are the routes times the periods.
- `plain`: `planning::PlainModel`, the example's own comparison - `std::map`s by the entities and loops
  over the data, the way the model is usually written without index sets. Concert only, as in the
  example, and up to I = 1700: its stock balance scans every machine's routes for each (item,
  period), so it grows with the square of the items.

## Both scenarios

**Backends**, one binary each (`build.sh`): `<scenario>_stub` against the lean Concert stub
(`../common/stub`: the containers alone, with a constraint and an objective that copy their terms as
Concert does), `<scenario>_cplex` against CPLEX Concert, `<scenario>_mathopt` against OR-Tools
MathOpt with HiGHS. CPLEX runs on one thread.

**Checks.** Every arm must hand the solver the same model: rows, columns and nonzeros as the solver
counts them (on the stub, the model's rows and nonzeros, the objective's coefficients and the
constraints' finite bounds, each summed).

## Running

```sh
bench/scenario/build.sh       # every scenario on the stub; on CPLEX and MathOpt where installed
cd bench/scenario
python3 run.py --scenario pmedian      # every backend built, L = 5000 to 50000, 5 repetitions
python3 run.py --scenario lotsizing    # I = 170 and 1700
python3 report.py ../results/<run>
```

`run.py` writes `runs.jsonl` and `meta.json` to `../results/<date>_<commit>_scenario_<scenario>_<size><levels>/`
and resumes a run it finds there; `--levels` picks other sizes, `--backends` and `--impls` fewer.
Each run is a fresh process; the arms of a repetition run under the same memory layout (an
environment padding and a heap seed of the repetition's own, ASLR on), as in the catalog. `wall_s`
is the whole process as the runner saw it, the solver's libraries loading included.

At L = 50000 p-median on MathOpt holds about 10 GB at its peak and takes about 40 s a run; the
p-median run takes about 15 minutes, the lot-sizing run about 5.

`report.py` gives, per backend, hand-written arm and size, each arm's model time, dreamor / that arm
for the model, to the solver's start and for the whole process (geometric mean over the repetitions
with a 95% interval), each phase's ratio, peak memory, and any check the arms disagree on; then the
headline, the geometric mean over the sizes.

## Results

On the home machine (AMD Ryzen 7 5700X, WSL2 - screening results, not a headline machine), on the
library this section was committed with (p-median before text name tokens were copied rather than
formatted, a path its integer names never take), 5 repetitions, every check agreeing. dreamor / each hand-written arm, the geometric mean over the
sizes; below 1, dreamor is faster. The headline is the time to the solver's start, the benchmarks'
measure:

| Scenario | Arm | CPLEX | MathOpt (HiGHS) | stub (containers alone) |
|---|---|---|---|---|
| p-median | hand, positional | 1.027 | 1.004 | 1.122 |
| lot sizing | flat, hand-optimized | 1.040 | 1.016 | 1.259 |
| lot sizing | plain, `std::map` | 0.664 | - | 0.272 |

- **Through a solver, dreamor builds what hand-optimized code builds in the same time:** within 4%
  in the geometric mean, and every size's 95% interval but one holds 1. Against the code usually
  written without index sets, it takes two thirds of the time through CPLEX and about a quarter on
  the stub.
- **On the stub, the gap to hand-optimized code is the rows dreamor makes.** A hand-coder who knows
  the rows are a product - locations x customers, routes x periods - computes positions instead of
  making them: p-median's 5e6 assignment rows cost dreamor 47 ms, lot sizing's `variables` phase is
  1.5 times flat's. Grouping such rows (p-median's `assign`, lot sizing's balance and capacity
  groups) is the rest: 1.2 to 1.7 times a loop that strides by position.
- **At 1e7 rows** (p-median at L = 100000, stub and CPLEX, 3 repetitions) the picture holds: CPLEX
  1.046 [0.939, 1.165], stub 1.074. MathOpt would need about 20 GB there and was not run.
