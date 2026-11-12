# Scenarios

The scenarios are layer 2 of the benchmarks. A scenario builds a whole model end to end, once with dreamor and once by
hand, up to the moment a solver starts on it. The [catalog](../catalog/index.md) measures one operation at a time over
data shapes. A scenario measures what those operations cost together in one model, including the cache effects between
them, and through the real solvers. If a scenario is slow, look for the catalog family that explains it.

## The two models

| Scenario | What it builds |
|---|---|
| p-median | The model of the modeling-language benchmarks, at their sizes: 100 customers, 5000 to 50000 candidate locations, 5e5 to 5e6 assignment variables. Dense, and indexed by ranges. |
| lot sizing | The repository's own example, as written: machines, items and periods, about 1e5 or 1e6 production rows. Sparse and keyed, its sums groups and slices, every variable named. |

Each runs on the lean Concert stub, on CPLEX, on OR-Tools MathOpt and on HiGHS's own C++ API, against hand-written arms:
hand-optimized code, the usual `std::map` code (lot sizing's, on Concert), and, on CPLEX and HiGHS, the solvers' own
matrix APIs as a lower bound. They are harsh on purpose: each is hard-coded for its one model and leaves out what dreamor
guarantees - contract checks, exception safety, generality ([Benchmarks](../index.md)).

## In short

The reference run measured dreamor 0.9.0 on an AMD Ryzen 7 5700X, under GCC 13 and Clang 18
([`results.md`](results.md)), to the solver's start:

- Through Concert and MathOpt, dreamor built what hand-written code builds within 5%: 1.01 to 1.05 times its time
  through CPLEX, and 1.00 to 1.04 through MathOpt.
- Against the code usually written without index sets, dreamor took 0.55 to 0.71 of the time through CPLEX.
- On HiGHS, `dreamor::highs` took 1.05 to 1.11 times as long as arrays filled by hand, and 0.29 to 0.47 of MathOpt's
  time to the same solver's start.

## Try it

```sh
cd bench/scenario
./build.sh
python3 run.py --scenario lotsizing --levels 170 --reps 1 --layout off   # one size, once
python3 report.py ../results/<run>                                       # report.md in the run's folder
```

A run that a conclusion rests on runs on bare metal, with every repetition under a layout of its own; `running.md`
says how.

## The documents

- [`design.md`](design.md): the two models, their phases and arms, what the arms are for, transparent huge pages, and
  the checks.
- [`running.md`](running.md): how a scenario is built, running it, and reading its report.
- [`results.md`](results.md): the reference run.
