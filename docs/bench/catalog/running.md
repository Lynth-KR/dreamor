# Running the catalog

How to build the catalog, run a family, read its report, and compare builds of dreamor. `design.md` describes the
families, the arms and the factors that these commands name. The catalog's code is in `bench/catalog/`, and every
command below runs there.

## Files

- `data.hpp` holds the cell, the data drawn for it, and the sample keys of the lookups. The data is a template over an
  arm's trait: the type of the container of each column's entities, and the type of the handle that a row holds.
- `driver.hpp` reads the arguments of one run, makes the data, calls the family, and prints the run as one JSON line.
- `shared.hpp` holds what the families share: the packed keys, the mixed hash, and the function that reads a finished
  expression into its check.
- `lookup.hpp`, `group.hpp`, `setop.hpp`, `slice.hpp`, `make.hpp`, `order.hpp` and `entities.hpp` describe each family
  and hold the parts that both arms use.
- `dreamor/` holds the dreamor arm: its trait and columns in `arm.hpp`, and one header for each family. `dreamor.cpp`
  is its main.
- `hand/` holds the hand-written arms: their trait in `arm.hpp`, and one header for each family. `hand.cpp` is their
  main.
- `build.sh` builds the binaries. `run.py` runs a family as a designed experiment. `report.py` summarizes a run, and
  `compare.py` compares two builds of dreamor.
- `bench/common/` holds what every benchmark under `bench/` shares, which the [benchmarks' overview](../index.md),
  "Shared pieces", lists.

The shared headers and the hand-written arms include no dreamor header, and `build.sh` builds them without an include
path to the library (the [benchmarks' overview](../index.md), "Measuring one side alone"). A phase that you add to a
family goes into both `dreamor/<family>.hpp` and `hand/<family>.hpp` (`entities.hpp` for the data family), under the
same name, with a check that every arm
computes in the same way.

## Build the binaries

Run `build.sh` before any run:

```sh
./build.sh               # build/catalog, catalog8, catalog_hand and catalog_hand8
LAYOUTS=5 ./build.sh     # the same, and a copy of each for each of 5 repetitions
```

- `build/catalog` and `build/catalog8` are the dreamor arm, built from `dreamor.cpp` with the library's include path.
- `build/catalog_hand` and `build/catalog_hand8` are the hand-written arms, built from `hand.cpp` without that include
  path, so a change to dreamor leaves them as they were (the [benchmarks' overview](../index.md), "Measuring one side
  alone").
- Every build uses `-std=c++20 -O2 -DNDEBUG -march=native` and the lean Concert stub in `bench/common/stub`.
- `CXX` picks the compiler: GCC 13 or later, or Clang 18 or later. The default is `g++`.
- `JOBS` sets how many compiles run at once. The default is half the cores.
- `LAYOUTS=n` also builds a copy of every binary for each of n repetitions, under `build/layout/<rep>/`. The copies
  are linked with the lld in `LLD_DIR`, by default LLVM 18's in `/usr/lib/llvm-18/bin`.
- `LAYOUT_SEED` sets the seed of the code layouts. Its default, 20261025, is the default `--seed` of `run.py`. If you
  run with another `--seed`, build with the same `LAYOUT_SEED`.

Each binary runs one cell and prints one JSON line, which is how `run.py` calls it. For example,
`build/catalog --family lookup --impl dreamor --k 3 --N 100000` runs the dreamor arm of `lookup` in one cell.
`driver.hpp` lists every argument.

## Run a family

`run.py` runs one family as a designed experiment. It runs every arm in every cell of the design, in an order that the
seed shuffles, each run in a fresh process of the arm's binary. A repetition number gives every arm the same seed, so
the arms are compared in pairs, and each repetition is a block.

```sh
python3 run.py --family lookup --reps 2                                  # N = 1e5 and 1e6: 1,600 runs
python3 run.py --family lookup --n-levels 100000 --reps 1 --layout off   # a quick trial: 400 runs
python3 run.py --family lookup --resolution 6 --reps 2                   # 32 cells of the six factors
python3 report.py ../results/<run>                                       # summary.md and cells.csv in the run's folder
```

By default, every repetition runs under a layout of its own. To run with `--reps 2`, build with `LAYOUTS=2` or more.

- `--impls a,b` runs those arms only. The default is every arm of the family.
- `--n-levels` and `--k-levels` set the levels of N and k. The defaults are `100000,1000000` and `1,2,3,5,8`. A family
  other than `lookup` and `make` leaves out k = 1.
- `--resolution 4` or `6` picks the fraction. The default is 4.
- `--reps` sets the repetitions, 2 by default. `--top-reps` lowers the repetitions at the largest N alone.
- `--seed` sets the seed of the data and of the layouts. The default is 20261025.
- `--lookups` sets the most keys that `hit`, `miss` and `half` look up. The default is 1,000,000.
- `--phases a,b` measures those phases only, for a run that looks at a few. The phases that later phases stand on
  still run unreported: `build`, `collect`, `load`, `index` and every `prep_*`.
- `--pmu` takes hardware counters, separated by commas: `cycles`, `instructions`, `l1d_miss`, `dtlb_miss`,
  `branch_miss`, or `name=0x<config>` for a raw event. For example, Zen 3's demand fills from L2, L3 and DRAM are
  `0x0143`, `0x0243` and `0x0843`. The counters form one group of at most 8, counted in user mode, and they show in
  each phase of `runs.jsonl`. If the CPU cannot count the whole group at once, the run fails rather than scale the
  counts. The default, `auto`, counts `instructions` if the machine lets a process count its own, and `meta.json`
  says whether it could. A virtual machine or a container may not allow it. `none` counts nothing.
- `--cpu` pins the runs to a CPU. The default, `auto`, picks one CPU of a physical core that does not hold CPU 0. On a
  virtual machine `auto` pins nothing, because pinning there pins a virtual CPU only. `none` pins nothing, and a CPU
  list pins to those CPUs.
- `--layout off` runs every repetition under the same layout, on the plain binaries of `build.sh`. The runs then have
  no environment padding, and their heap is not preconditioned.
- `--timeout` sets the seconds that one run may take, 1800 by default.
- `--out` names the results folder.
- `--dry-run` prints the results folder and the number of runs, and runs nothing. It still creates the folder.
- `--builds` and `--reuse` are described below.

To make a result that a conclusion rests on, run on bare metal with the CPU's frequency fixed: boost off, and the
`performance` governor. Let `--cpu auto` pin the runner to one core, as the reference run in `results.md` was pinned.
A run under WSL2 or another virtual machine is not pinned, and it is good for screening only.

### The results folder

A run writes to `bench/results/<date>_<commit>_catalog_<family>_N<levels>_res<4|6>/`. The folder of the data family ends
in `_full` instead of `_res<4|6>`. With `--builds`, the folder's name ends in the build names, such as `_base-vs-head`.
Uncommitted changes and resuming a run are as in every experiment (the [benchmarks' overview](../index.md), "What every
experiment holds to").

- `runs.jsonl` holds one line for each run. A line holds the cell, the arm and the repetition, the rows and the k1s of
  the data, the peak resident memory, and the CPU at the start and at the end. Each phase has its wall, user and
  system time in milliseconds, its minor page faults, and its counters. `setup_ms` holds the setup stages, and
  `checks` holds each phase's check. `skipped` holds the reason if the arm cannot run the cell. `run.py` adds the
  run's status, its place in the order, its wall time and its environment padding.
- `meta.json` holds the design, the machine, the compiler and the commit. The compiler is the one that `CXX` names
  when `run.py` starts, so run it with the `CXX` that built the binaries.

The setup stages run before the first phase, and no ratio includes them. Every run has `heap`. Every family but the
data family has `data`. `lookup` has `samples`, `slice` has `drivers`, and the data family has `keys`.

## Read the report

`python3 report.py ../results/<run>` prints Markdown, and writes `summary.md` and `cells.csv` in the run's folder.

- A ratio is a hand-written arm's time over dreamor's time. Above 1, dreamor is faster.
- Ratios are geometric means over the cells that the arm ran in.
- `best` is the fastest hand-written arm in each repetition and phase of a cell, so it may be a different arm in
  each.
- The 95% interval of a ratio comes from the variation between the repetitions of a cell. With `--layout on`, that
  variation includes the variation between layouts.
- An effect is the difference of the mean ln(ratio) between the two levels of a factor. The report shows it as the
  multiplier that the high level puts on the ratio, with its standard error.

The report has these parts:

- The number of runs, and the check mismatches. Every arm that ran a cell's repetition must agree on every check.
- Coverage: the cells that each arm ran, and why an arm was unsupported in the others.
- The absolute times of each arm for each N, in milliseconds.
- The variation of each arm between the repetitions of a cell, as a percentage.
- The ratios of each hand-written arm and of `best`, with their intervals, and how often each arm was the fastest.
- The effects on the ratio of `best` and of each hand-written arm: the two-level factors, then the ratio at each
  level of k and N.

`cells.csv` holds one line for each cell, comparison and phase, with the ratio and the number of repetitions.

## Compare two builds of dreamor

`--builds base=<path>,head=<path>` compares two builds of `build/catalog`, such as the library before and after a
change. Every run of the plan runs once for each build, the two runs one right after the other in a random order,
under the same seed and layout.

```sh
python3 run.py --family setop --impls dreamor --reps 3 --builds base=<old>/catalog,head=build/catalog
python3 compare.py ../results/<run>
```

- For k = 8, `run.py` runs `<path>8` beside each build, so build both.
- With `--layout on`, each build needs its copies under `<its folder>/layout/<rep>/`.
- The hand-written arms do not depend on the build. So pass `--impls dreamor` to run the dreamor arm alone.

`compare.py` prints Markdown and writes `compare.md` in the run's folder. A ratio is the time of the build named second
in `--builds` over the time of the build named first, so a ratio below 1 means that the second build is faster.

- For each phase and N, it gives the geometric mean ratio over the cells, with a 95% interval from the spread of the
  cells, and how many cells moved past 5% either way.
- For each phase and k, it gives the ratio at each k. A cost that grows with the columns shows only in this table.
- It lists the cells that moved most.
- It lists the results whose checks differ between the builds. A change of speed compares like with like only if the
  results agree.
- With counters, it gives each counter's ratio beside the time. Instructions barely move with the cache or the heap.
  So a time ratio that the instructions do not follow comes from memory, and one that they follow comes from the code
  that the compiler made.

A phase's time includes what the phases before it left in the heap. So a phase whose code did not change can move
too. Before you read such a move as an effect, measure the phase alone with `--phases`.

## Measure one side against earlier runs

`--reuse <earlier run>` copies the runs of every arm whose binaries an earlier run of the same plan measured, and runs
the other arms. After a change to dreamor, it runs only the dreamor arm:

```sh
python3 run.py --family lookup --resolution 6 --reps 2 --reuse ../results/<earlier run>
```

The hand-written arms are all in `catalog_hand`, so a change to one of them changes the binary of the others.
`--reuse-arms` then names the arms to copy, those whose own code did not change:

```sh
python3 run.py --family lookup --resolution 6 --reps 2 --reuse ../results/<earlier run> --reuse-arms dreamor,map
```

`run.py` copies the runs only if the earlier run has the same family, arms, design, levels, repetitions, seed, lookups,
counters and phases, and the same CPU model, pinned CPUs and compiler. How the copies are checked, and the controls that
tell whether the machine has moved since, are in the [benchmarks' overview](../index.md), "Measuring one side alone".
