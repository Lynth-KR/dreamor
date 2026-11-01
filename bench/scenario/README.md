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
`IloCplex::extract`, MathOpt's `NewIncrementalSolver`, HiGHS's `passModel`, the Callable Library's
`CPXnewcols` and `CPXaddrows`) and `solve0` (a solve with a time limit of zero, where the benchmarks
stop the clock). Both are recorded, and their sum is the time to the solver's start. How a solver's API
splits them is its own: Concert hands part of the model over only once it is used, in `solve0`, so
`extract` alone understates Concert. Against the stub there is no solver, so a run stops after `budget`.

**Arms.**
- `dreamor`: sets with `makeSet`, `VarTable`s, the sums with `sumOf`, written as the README writes a
  model - the assign constraints iterate the customers and take each one's `groupBy` slice, the link
  constraints walk the set and look x and y up by the row's columns.
- `hand`: the model written straight against the solver's API, as the benchmarks' C++ baselines
  are - variables in `std::vector`s indexed by position (`x[l * C + c]`), the sums as loops. On
  HiGHS, whose C++ API is a matrix API, the rows go by position into the arrays of one `HighsLp`,
  handed over in one `passModel` (in `extract`).
- `calls` (HiGHS): the same rows, an `addRow` a row, as code written against HiGHS's API one
  constraint at a time does.
- `callable`, `callable-rows` (CPLEX): CPLEX's Callable Library, the C API under Concert, as its own
  examples write a model - the arrays filled by position and handed over in one `CPXnewcols` and one
  `CPXaddrows` (in `extract`), or a `CPXaddrows` a row.

On Concert and MathOpt both arms add a constraint at a time and end each expression once it is in
the model, as Concert code does; in MathOpt the hand-written arm moves its expressions into the
constraints.

## Lot sizing

The repository's own example (`examples/production_planning*.hpp`), its model and its data as they
are, the same in every repetition: M = 20 machines, each routing about 60% of I items, over T = 50
periods - about 600 production rows an item; I = 170 and 1700 give 1e5 and 1e6 rows. Yield and Setup
on every (machine, item, period) of the routes, Stock on every (item, period); a stock balance an
(item, period), a capacity a (machine, period), a setup link a production row, the costs of holding
and of setups. Every variable is named, as the example names them. Where p-median is dense and
indexed by ranges, this model is sparse and keyed: its rows are generated from the routes, its sums
are groups and slices.

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
- On HiGHS, `dreamor` is `planning::HighsApiModel` and `flat` fills one `HighsLp`, names included,
  for one `passModel`; on CPLEX, `callable` and `callable-rows` are flat's model through the
  Callable Library, as in p-median.

## Both scenarios

**Backends** (`build.sh`): `<scenario>_stub` against the lean Concert stub
(`../common/stub`: the containers alone, with a constraint and an objective that copy their terms as
Concert does), `<scenario>_cplex` against CPLEX Concert, `<scenario>_mathopt` against OR-Tools
MathOpt with HiGHS, `<scenario>_highs` against HiGHS's own C++ API (`$HIGHS_DIR`, or the HiGHS the
OR-Tools prebuilt ships). Every solver runs on one thread. Each is built twice, into `build/`: `<scenario>_<backend>` holds
the dreamor arm, `<scenario>_<backend>_hand` the hand-written arms, with no dreamor header but the logger, so a
change to the library leaves them as they were and they need measuring once per compiler and machine.

**What the arms are for.** dreamor is an expression API, and the API it stands against is the
expression API a backend's users write in: Concert on CPLEX, MathOpt on HiGHS - and for
`dreamor::highs`, which writes HiGHS's own API in highspy's terms, MathOpt is the rival. The matrix
arms - `hand` on HiGHS, `callable` on CPLEX - are a lower bound, not a rival: C-style code that fills
the solver's arrays by position, which hardly anyone writes by hand, the way few write the Callable
Library though it is there. Vendor against vendor, Concert by hand against CPLEX's own Callable
Library, says what an expression API costs over the arrays; `report.py` sets every arm against its
backend's matrix arm, `dreamor::highs` included.

**Transparent huge pages** are left off, the default (`docs/guide.md`, "Large models"): with them on,
every arm spends far less in page faults - on p-median at L = 20000, a third to nearly two thirds
less up to the solver's start - and the ratios between arms move.

**Checks.** Every arm must hand the solver the same model. Each run checks the rows, columns and
nonzeros as the solver counts them (on the stub, the model's rows and nonzeros, the objective's
coefficients and the constraints' finite bounds, each summed). Before a run, `equivalence.py` checks the
models themselves: at each scenario's smallest size, every arm's model as the solver holds it, compared
with dreamor's by meaning - bounds, integrality, costs, right-hand sides and coefficients.

## Running

```sh
LAYOUTS=5 bench/scenario/build.sh   # every scenario on the stub, on CPLEX, MathOpt and HiGHS where installed,
                                    # and a copy of each per repetition for 5
cd bench/scenario
python3 equivalence.py                 # every arm hands each solver dreamor's model
python3 run.py --scenario pmedian      # every backend built, L = 5000 to 50000, 5 repetitions
python3 run.py --scenario lotsizing    # I = 170 and 1700
python3 run.py --scenario pmedian --reuse ../results/<earlier run>   # a new dreamor: its arm alone
python3 report.py ../results/<run>
```

`run.py` writes `runs.jsonl` and `meta.json` to `../results/<date>_<commit>_scenario_<scenario>_<size><levels>/`
and resumes a run it finds there; `--levels` picks other sizes, `--backends` and `--impls` fewer.
Each run is a fresh process; the arms of a repetition run under the same memory and code layout (an
environment padding, a heap seed and the copies `LAYOUTS` links, all the repetition's own, ASLR on), as in
the catalog; `--layout off` runs every repetition on `build.sh`'s plain binaries. `wall_s`
is the whole process as the runner saw it, the solver's libraries loading included. `--reuse`, as in the
catalog, takes the hand-written arms' runs from an earlier run whose `_hand` binaries are the same, runs
dreamor's arm and a tenth of the hand-written runs (at least 30) as controls, and the report says whether
the machine has moved.

At L = 50000 p-median on MathOpt holds about 7 GB at its peak and takes about 21 s a run; the
p-median run takes about 13 minutes, the lot-sizing run about 4.

`report.py` gives, per backend, hand-written arm and size, each arm's model time, dreamor / that arm
for the model, to the solver's start and for the whole process (geometric mean over the repetitions
with a 95% interval), each phase's ratio, peak memory, and any check the arms disagree on; then the
headline, the geometric mean over the sizes; then, on CPLEX and HiGHS, each arm / the matrix arm,
and the time to the solver's start across backends.

## Results

The reference run on dreamor 0.5.0: AMD Ryzen 7 5700X, bare metal, boost off and the `performance`
governor, pinned to one core; built with GCC 13.3 and with Clang 18.1; 5 repetitions, each under a
memory layout and a code layout of its own, every check agreeing. dreamor / each hand-written arm,
the geometric mean over the sizes, GCC 13 / Clang 18 in each cell; below 1, dreamor is faster. The
headline is the time to the solver's start, the benchmarks' measure:

| Scenario | Arm | CPLEX | MathOpt (HiGHS) | HiGHS | stub (containers alone) |
|---|---|---|---|---|---|
| p-median | hand, positional | 1.036 / 1.035 | 1.004 / 1.011 | 1.064 / 1.081 | 1.079 / 1.141 |
| p-median | a row at a time (`callable-rows`, `calls`) | 1.848 / 1.885 | - | 0.528 / 0.541 | - |
| lot sizing | flat, hand-optimized | 1.037 / 1.033 | 1.029 / 1.031 | 1.113 / 1.120 | 1.307 / 1.305 |
| lot sizing | a row at a time (`callable-rows`) | 2.378 / 2.407 | - | - | - |
| lot sizing | plain, `std::map` | 0.605 / 0.606 | - | - | 0.236 / 0.246 |

- **Through Concert and MathOpt, dreamor builds what hand-written code builds within 5%:** 1.03-1.04
  through CPLEX and 0.99-1.04 through MathOpt, at every size and on both compilers. Against the code
  usually written without index sets, it takes 0.53-0.69 of the time through CPLEX and 0.20-0.29 on
  the stub.
- **On HiGHS, `dreamor::highs` takes 1.06-1.12 times the arrays filled by hand**, the lower bound:
  its model phases take about twice the hand arm's (1.8-2.2), building expressions where the hand
  arm writes the arrays, and the rest - `passModel` and HiGHS's own start - is the same. An `addRow`
  a row takes nearly twice as long (dreamor / `calls` 0.52-0.55). MathOpt, the expression API on the
  same solver, is the rival: `dreamor::highs` reaches HiGHS's start in 0.29-0.36 of MathOpt's time
  on p-median and 0.32-0.48 on lot sizing.
- **Against CPLEX's Callable Library, the cost is Concert's:** to the solver's start, dreamor on
  Concert takes 3.3-3.8 times the matrix arm and Concert written by hand 3.1-3.7 times.
- **On the stub, the gap to hand-optimized code is the rows dreamor makes.** A hand-coder who knows
  the rows are a product - locations x customers, routes x periods - computes positions instead of
  making them: p-median's 5e6 assignment rows cost dreamor 40 ms, lot sizing's `variables` phase is
  1.4-1.7 times flat's. Grouping such rows (p-median's `assign`, lot sizing's balance and capacity
  groups) is the rest: 1.2 to 1.9 times a loop that strides by position.
- **At 1e7 rows** (p-median at L = 100000, 5 repetitions) the picture holds: CPLEX 1.041 / 1.039,
  MathOpt 1.004 / 1.012 (about 13 GB at the peak), HiGHS 1.082 / 1.096, stub 1.095 / 1.143.
