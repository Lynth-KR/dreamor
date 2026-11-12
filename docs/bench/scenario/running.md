# Running the scenarios

How the scenarios are built and run, and how to read their report. The scenarios' code is in `bench/scenario/`;
the commands below say where they run.

## How a scenario is built

`build.sh` builds each scenario against each backend that is installed:

- `<scenario>_stub` against the lean Concert stub in `bench/common/stub`, which leaves only the containers to measure.
- `<scenario>_cplex` against CPLEX Concert, at `$CPLEX_STUDIO_DIR`. The default is
  `/opt/ibm/ILOG/CPLEX_Studio2212`.
- `<scenario>_mathopt` against OR-Tools MathOpt, which solves with HiGHS, at `$ORTOOLS_DIR`. The default is
  `/opt/ortools`.
- `<scenario>_highs` against HiGHS's own C++ API, at `$HIGHS_DIR`. The default is the HiGHS that the OR-Tools prebuilt
  ships.

A backend that is not installed is skipped. Every solver runs on one thread. `CXX` picks the compiler: GCC 13 or later,
or Clang 18 or later. The default is `g++`.

Each backend gets two binaries in `build/`:

- `<scenario>_<backend>` is the dreamor arm, built from `<scenario>_dreamor.cpp` with dreamor's include path.
- `<scenario>_<backend>_hand` holds the hand-written arms, built from `<scenario>_hand.cpp` without dreamor's include
  path. A dreamor header in it does not compile. So a change to the library leaves this binary byte for byte as it
  was, and you need to measure it only once for each compiler and machine.

A binary that is asked for an arm it does not hold records a skip and measures nothing. The files that both binaries
include hold no dreamor header:

- `<scenario>.hpp` holds the instance, the arguments, `main`'s skeleton and the JSON line.
- `backend.hpp` holds the backend's plumbing: how a model is handed to the solver, started and checked.

`backend_dreamor.hpp` adds what the dreamor arm alone needs: the backend's dreamor header, and the hand-over of a
`dreamor::highs::HighsEnv` to HiGHS.

## Running

```sh
LAYOUTS=5 bench/scenario/build.sh   # every backend installed, plus one copy of each binary per repetition, for 5
cd bench/scenario
python3 equivalence.py                 # every arm hands each solver dreamor's model
python3 run.py --scenario pmedian      # every backend built, L = 5000 to 50000, 5 repetitions
python3 run.py --scenario lotsizing    # I = 170 and 1700
python3 report.py ../results/<run>
```

To try a scenario quickly, without the layout copies, run one size once:

```sh
python3 run.py --scenario lotsizing --levels 170 --reps 1 --layout off
```

To measure a new version of dreamor against the hand-written arms of an earlier run, pass `--reuse`:

```sh
python3 run.py --scenario pmedian --reuse ../results/<earlier run>
```

`--reuse` copies the earlier run's runs of every arm whose binaries are the same, and runs the other arms, and a tenth
of the copied runs, at least 30, as controls. After a change to one hand-written arm, which changes the `_hand` binary
of all, `--reuse-arms` names the arms to copy, such as `--reuse-arms dreamor,plain`. The report then says whether the
machine has moved. The [benchmarks' overview](../index.md), "Measuring one side alone", gives the conditions.

Other options:

- `--levels` picks other sizes.
- `--backends` and `--impls` pick fewer backends and arms.
- `--layout off` runs every repetition on `build.sh`'s plain binaries, with no environment padding and no heap
  preconditioning. By default, `run.py` needs the copies that `LAYOUTS` builds.
- `--seed` sets the seed of the run order, the layouts and p-median's instances. The default is 20261025, which is
  also `build.sh`'s default `LAYOUT_SEED`.

`run.py` writes `runs.jsonl`, one line for each run, and `meta.json` to
`bench/results/<date>_<commit>_scenario_<scenario>_<size><levels>/`. If it finds that folder, it resumes the run. Each
run is a fresh process, and the runs come in a seeded random order. Each repetition runs under a memory layout and a
code layout of its own: an environment padding, a heap seed and the copies that `LAYOUTS` links, as in the catalog. ASLR
stays on. A run's `wall_s` is the whole process as the runner saw it, including the time to load the solver's libraries.

At L = 50000, p-median on MathOpt holds about 7 GB at its peak and takes about 21 s to the solver's start, 26 s
a run. The whole p-median run takes
about 13 minutes, and the lot-sizing run about 4.

## Reading the report

`report.py` prints Markdown and writes it to `report.md` in the run's folder. A ratio is dreamor's time over the other
arm's: below 1, dreamor is faster. Each ratio is the geometric mean of the repetitions' ratios. The table for each size
and the table across the backends give it with a 95% interval. The report gives these parts:

- If runs were copied with `--reuse`, a line on the controls: whether the machine has moved; and a line for each arm
  copied although its binary changed.
- Each backend, arm and size whose checks differ from dreamor's.
- For each backend, hand-written arm and size: each arm's median time to build the model, and dreamor's ratio for the
  model, to the solver's start, and for the whole process. Beside them, each arm's median peak memory.
- For each backend and hand-written arm, each phase's ratio.
- The headline: for each backend and arm, the geometric mean of dreamor's ratio over the sizes.
- On CPLEX and HiGHS, each arm's time over the time of the backend's matrix arm (`callable` on CPLEX; on HiGHS,
  p-median's `hand` and lot sizing's `flat`), for the model and to the solver's start.
- Each arm across the backends that solve: its median time to the solver's start on each backend, and each backend's
  time over MathOpt's.
