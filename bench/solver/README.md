# Solver probe

Not a benchmark. The catalog (`../catalog/`) measures the containers against a stub, and the
scenarios (`../scenario/`) whole models through the real solvers. This probe looks at one thing a
real solver does with the variables it is handed, apart. It answers two questions:

- **How much of a real build are the containers?** The same build against the stub and against
  the solver: their ratio is the containers' share. It says how much a gain on the stub can be
  worth to a user.
- **Is a gap between the arms under the solver the containers' at all?** Usually not: see below.

It builds the variables of one cell (K1 x K2 x K3, every route kept, ids in loop order, F
families), both arms, one fresh process per run:

- `dreamor`: `makeSet(...).collect()`, then one `VarTable` per family.
- `flat`: rows as ids, one `std::vector` of variables per family, an `unordered_map` index.

## Why the catalog measures against a stub

What a real solver spends creating variables moves with its insides and the CPU, not with the
containers in front of it. Concert is the plainest case:

- **Shared domains.** Concert keeps one domain per distinct (lb, ub) in a hash table
  (`IloEnvI::getOrMakeSharedDomain`). With many distinct bounds - the catalog's tags, a
  capacity per variable - the table grows to hundreds of thousands of entries, every variable
  looks its bounds up in it, and each lookup walks a chain of cache misses.
- **A cliff in the caller's instruction count.** How fast those lookups go depends on how much the
  caller does between two variables. With little, the processor overlaps one variable's misses
  with the next one's; past some point it cannot, and the build slows sharply. A few dozen
  register-only instructions per variable, touching no memory, are enough to cross it, in a
  hand-written loop as in dreamor's. `--pads` adds exactly that, in both arms.

Where the cliff lies is Concert's and the CPU's business, and it falls on both arms alike. A
flat / dreamor ratio measured through a real solver mixes the containers with it, so the
catalog measures against a stub, the scenarios report a build through each solver beside the same
build on the stub, and this probe takes the cliff apart.

## Bounds and pads

- `--bounds unique`: every variable its own upper bound, its number among all the variables.
  Strictly one domain per variable: the worst case for Concert's domain table, kept as such. (The
  catalog's tags come close but repeat.)
- `--bounds few`: 16 distinct upper bounds, as capacities and big-Ms tend to be.
- `--pads`: 0, 32, 128 or 512 register-only instructions added to every bound computation, in
  both arms; other counts are refused. A ratio that moves with the pad measures the solver's
  memory accesses, not the containers.

## Running

```sh
bench/solver/build.sh        # probe_stub always; probe_concert and probe_mathopt where installed
cd bench/solver
python3 run.py               # 1e6 rows, 3 families, 5 repetitions, unique and few, pads 0 and 32
```

`build.sh` finds CPLEX at `$CPLEX_STUDIO_DIR` (default `/opt/ibm/ILOG/CPLEX_Studio2212`) and
OR-Tools at `$ORTOOLS_DIR` (default `/opt/ortools`), and skips what is missing. `run.py` writes
`probe.csv` to `results/<date>_<commit>_solver_N<rows>/` beside the other runs and prints,
per backend, bound mode and pad, both arms' median build, flat / dreamor, and each arm's container
share.

## Reading it

- **The containers' share of a build** - the build against the stub over the build against the
  solver, per arm - is the useful number: it says how much of a real build the containers are,
  so how much a gain on the stub can be worth to a user. It moves with the bounds: few
  distinct bounds leave Concert little to do per variable, a bound per variable a lot.
- **flat / dreamor under a solver** is for the machine it ran on only, and is read as "is there a
  gap the containers explain", not as a ranking. A ratio that moves when `--pads` changes is the
  solver's memory accesses, not the containers.
- MathOpt shares no domains, so it has no such cliff; its own internals are as opaque.
