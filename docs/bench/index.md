# Benchmarks

These benchmarks answer one question: how much does dreamor cost, or save, against the same model code written by hand?
Two layers answer it, and a study answers a narrower question beside them. Each has its own folder and runner under
`bench/`. Their documents are on these pages.

The hand-written arms are harsh opponents on purpose. Each is written for its one operation on its one data, with that
operation's logic hard-coded, and it leaves out what dreamor guarantees: the checks of its contracts, its exception
safety, and the generality that takes any columns and any ids. A name, for example, is written with no check of its
length, no overflow policy and no switch to turn naming off. A packed key works only while the ids fit in 64 bits, and
a direct array only while it stays small. So where dreamor matches such code, its guarantees cost nothing there, and
where such code wins, the results say what it knew of the data.

## The two layers

- **[The catalog](catalog/index.md) measures one operation at a time.** Each family of operations is one designed
  experiment: lookups, grouping, set operations, slices, generation, order, and entities with solution values. Each
  family sets dreamor against hand-written arms over the shapes that data takes and the ways that models reach it. The
  factors are the number of columns, the number of rows, and six two-level factors: shared or owned entities, density,
  skewed list sizes, group size, row order and id order. By default, a family runs those six factors as a
  resolution IV screening fraction. The family of entities has two factors of its own instead: the key's type and the
  order of access. A result therefore says which shape and which access pattern it holds for. Every
  catalog binary is built against the lean Concert stub, so that only the containers are measured.
- **[The scenarios](scenario/index.md) measure whole models.** A scenario builds one model end to end, with
  dreamor and by hand, up to the moment a solver starts on it. This is the measure of the modeling-language benchmarks.
  The scenarios run on the stub, on CPLEX, on OR-Tools MathOpt and on HiGHS's own C++ API. One model is p-median, the
  model that those benchmarks use. The other is the repository's lot-sizing example, as it is written. The hand-written
  arms include hand-optimized code and the usual `std::map` code. The headline is the time to the solver's start. If a
  scenario is slow, the catalog family that explains it is the place to look. By default, MathOpt and HiGHS's own API
  hand the same model to the same HiGHS, so the scenarios also compare those two ways to reach HiGHS.

## A study

- **[The visit-order study](visit-order.md)** asks how good dreamor's visit order for a set's sources is, against
  every other order of the same sources.

## Shared pieces

The folder `bench/common/` holds what the experiments share:

- `measure.hpp` measures a phase. A phase's measures are its wall time, the process's user and system time, the page
  faults, and the hardware counters that `--pmu` names. The file also reads the resident memory and its peak. Before
  a run, it preconditions the heap: it allocates 65,536 blocks of random sizes and frees a random half of them, by the
  repetition's heap seed, because a fresh heap is an allocator's best case.
- `result.hpp` collects one run's phases, setup stages and checks, and writes them as fields of one JSON line. The
  runner's binary adds the run's own fields, such as the arm and the size. A setup stage, such as making the
  instance, is in no phase, so only a scenario's ratio for the whole process includes it.
- `stub/` is the lean Concert stand-in, described below.
- `machine.py` records the machine and the code that a run measures. It chooses the CPU that `--cpu auto` pins to,
  and it records the governor, boost, SMT, transparent huge pages and the last-level cache in the meta file of a
  catalog or scenario run. It decides what `--pmu auto` counts, and it names the checkout's commit, with a hash of
  its uncommitted changes, when the run starts. `test_machine.py` tests the CPU choice against fake `/sys` trees.
- `layout.py` derives each repetition's code layout from the run's seed: the seed with which the linker shuffles the
  code's sections, and a padding laid among them. A runner checks that every copy of a binary was built with the
  run's seed, and records each copy's sha256.
- `build_lib.sh` is sourced by the catalog's and the scenarios' `build.sh`. It runs the compiles `JOBS` at a time,
  half the cores by default. If `LAYOUTS=n` is set, it writes the link options for repetitions 0 to n-1, which link
  with the lld in `LLD_DIR`, LLVM 18's by default.
- `reuse.py` implements `run.py --reuse`, which "Measuring one side alone" below describes.
- `compare_mps.py` compares two MPS files by what the models mean, not by how a writer spells them. The scenarios'
  `equivalence.py` uses it.

To run the tests of `machine.py`:

```sh
cd bench
python3 -m unittest common.test_machine
```

### The lean Concert stub

`bench/common/stub/` stands in for Concert's headers when a benchmark measures the containers alone. A variable is a
16-byte heap object behind a handle, as in Concert. An expression records its variables and their coefficients. A
constraint or an objective keeps a copy of its terms, so that the expression it came from can be ended, as in Concert.
The stub keeps no names. So the solver side costs the smallest constant that a design of handles over heap objects
allows, and that constant is the same in every arm. The stub mimics Concert's outward costs, not its insides. What a
real solver spends is measured through the real solvers, in the scenarios.

The stub is not `tests/stub/`. That stub stores names and keeps every expression alive for the tests to inspect, which
would put most of the memory and the time on the solver side here.

## What every experiment holds to

- **Every arm does the same work.** In the catalog and the scenarios, each run records checks that every arm must
  agree on, such as the rows, the variables' bounds, or the model's rows and nonzeros. The catalog's report counts the
  checks on which arms disagree, and a scenario's report names each backend, arm and size where they do. In the visit-order study, every order must make the same set.
- **Each run is a fresh process.** A model is built once, so every measurement is a cold one.
- **Each repetition runs under a memory layout and a code layout of its own.** In the catalog and the scenarios, every
  repetition gets an environment padding, a heap seed and copies of the binaries linked with its own code layout.
  Every arm of a repetition gets the same layout, so the arms are compared in pairs. ASLR stays on. This follows
  Mytkowicz et al. (ASPLOS 2009), Curtsinger & Berger (ASPLOS 2013) and van Kempen & Berger (ISMM '26). To run every
  repetition on the plain binaries instead, with no padding and no heap seed, pass `--layout off`. The visit-order study runs on the plain binaries.
- **Machines are not compared.** Conclusions are drawn between arms on one machine. In the catalog and the scenarios,
  each run's `meta.json` records the machine: the CPU, the virtualization, the pinned CPU, the compiler and the commit.
  The visit-order study prints the CPU's model only.
- **Results go to `bench/results/`**, which git ignores. Each run gets a folder of its own,
  `<date>_<commit>_<experiment>...`. If tracked files have uncommitted changes, `-dirty-<hash of the diff>` follows the
  commit. If the catalog or a scenario finds its folder already there, it resumes the run. The folder name holds the
  date, so a rerun on another day starts a new folder unless `--out` names the old one. The visit-order study
  overwrites its CSV file instead.
- **CI does not run the benchmarks.** Shared runners are too noisy for timing. CI checks that they compile:
  `tools/check_bench.py` parses every variant that the `build.sh` files build, except CPLEX's, and links nothing.

### Measuring one side alone

The catalog's and the scenarios' hand-written arms are built into binaries of their own, and the compiler enforces the
separation. A hand-written binary is compiled without the include path to dreamor's headers, so a dreamor header in it
does not compile. A change to dreamor therefore leaves the hand-written binaries byte for byte as they were, and they
need measuring only once per compiler and machine. A change to a hand-written arm leaves dreamor's binary as it was.

`run.py --reuse <earlier run>` copies the earlier run's runs of every arm whose binaries are unchanged, and runs the
other arms: a new version of dreamor against the hand-written arms measured before, or a changed hand-written arm
against dreamor. It takes the copies only if all of these hold:

- Both runs have the same CPU, pinning, compiler, counters, seed, repetitions and design. The fields that a runner
  compares are the `SAME` list of its `run.py`.
- Both runs used `--layout on`.
- Every repetition's copy of every binary of a copied arm has the sha256 that the earlier run recorded.

The hand-written arms share one binary, so a change to one of them changes the binary of all. `--reuse-arms` names the
arms to copy instead, such as `--reuse-arms dreamor,map` after a change to another hand-written arm. An arm named
there is copied even if its binary changed, because you vouch that its own code did not.

The machine itself may have changed since the earlier run. So a tenth of the copied runs that measured something, at
least 30, or all of them if there are fewer, run again as controls. The seed draws them across the cells and the
repetitions. An arm copied although its binary changed gets at least 30 of its own: the code around it may have moved
it. The report compares the controls' time with the copies' time, for all the controls and for each such arm. If the
95% interval of a ratio reaches outside 0.98 to 1.02, the report warns, and you must run the whole experiment again.
The controls are left out of every ratio.

## Checking a change

To check a change to an operation that the benchmarks exercise, compare the build before the change with the build
after it. The catalog's `run.py --builds base=<old>,head=<new>`, where `<old>` and `<new>` are the paths of two builds
of `build/catalog`, runs every cell once for each build, the two builds one right after the other. Its `compare.py`
gives each phase's ratio with its interval, and the cells that moved. If a whole model would feel the change, run the
scenarios as well. `docs/dev/benchmark.md` says when a change needs these checks, and gives the quick runs that confirm
that every arm still does the same work.
