# Benchmarks

How much does dreamor cost, or save, against the same model code written by hand? Two layers answer
it, each a designed experiment with its own runner and README:

- **[catalog/](catalog/README.md), layer 1: one operation at a time.** Each family of operations -
  lookups, grouping, set operations, slices, generation, order, entities and solution values - is
  measured against hand-written arms over the shapes data takes and the ways models reach it (k
  columns, density, fan-out, group size, row and id order, ...), a resolution IV screening design
  per family. A result says which shape and access pattern it holds for. Against the lean Concert
  stub, so only the containers are measured.
- **[scenario/](scenario/README.md), layer 2: whole models.** A model built end to end by dreamor
  and by hand, up to the moment a solver starts on it - the modeling-language benchmarks' measure -
  on the stub, CPLEX and MathOpt: p-median, the model those benchmarks use, and the repository's
  lot-sizing example as it is written, against hand-optimized code and against the usual `std::map`
  code. The headline is the time to the solver's start; a slow scenario points back to the catalog
  family that explains it.

Beside them, two studies that answer one question each:

- [solver/](solver/README.md): how a real solver's variable creation meets the caller's code -
  Concert's shared bound domains and the cliff they put in a build.
- [plan/](plan/README.md): `makeSet`'s visit order against every other order of the same sources.

## Shared pieces

- `common/measure.hpp`: wall, user and system time, page faults and hardware counters (`--pmu`) per
  phase, peak memory, and the heap preconditioning every run starts with.
- `common/result.hpp`: one run's phases, checks and setup stages as one JSON line.
- `common/stub/`: the lean Concert stand-in. A variable is a 16-byte heap object behind a handle, an
  expression a vector of (variable, coefficient), a constraint or an objective a copy of its terms:
  the solver side at the smallest constant a handle-over-heap-object design allows, the same in
  every arm. It mimics Concert's outward costs, not its insides - what a real solver spends is
  measured through the real solvers, in the scenarios. (It is not `tests/stub/`, which keeps names
  and expressions for the tests to inspect.)
- `common/machine.py`: the CPU choice and the machine record each run's meta file carries;
  `python3 -m unittest common.test_machine` tests it against fake `/sys` trees.

## What every experiment here holds to

- **Every arm does the same work.** Each phase hands out a check (the rows, the variables' bounds,
  the model's rows and nonzeros) that every arm must agree on; the reports list any that do not.
- **One run, one fresh process.** A model build is a one-shot, so every measurement is a cold one,
  and the arms of a repetition run under the same memory layout - an environment padding and a heap
  seed of the repetition's own (Mytkowicz et al., ASPLOS 2009; van Kempen & Berger, ISMM '26).
- **Machines are not compared.** Conclusions are drawn between arms on one machine; each run's meta
  file records which (CPU, virtualization, pinning, compiler, commit).
- **Where results go.** `results/<date>_<commit>_<experiment>.../`, one folder a run, ignored by
  git; uncommitted changes add `-dirty-<hash of the diff>` to the commit. Running the same thing
  again resumes it.
- **Not in CI.** Shared runners are too noisy for timing.

## Checking a change

A change to an operation the benchmarks exercise is compared build against build: the catalog's
`run.py --builds base=<old>,head=<new>` runs every cell once per build, side by side, and
`compare.py` gives each phase's ratio with its interval and the cells that moved (catalog README,
"Comparing builds"). A change a whole model would feel is run through the scenarios as well.
