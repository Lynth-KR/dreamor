# Container benchmark

How much does dreamor cost, or save, compared with writing the same model code by hand? This
benchmark measures it without a solver, as a designed experiment: the same indexed variable
families built three ways, the same rows, the same variables, the same expressions.

It is read against a stage, not in a vacuum: the operations, their mix and their sizes are those
of the lot-sizing example (`examples/production_planning.hpp`), the kind of model dreamor is
written for.

## The stage

The set is `{(k1, k2, k3) : k2 in C2(k1), k3 in C3(k1), keep(k1, k2, k3)}`: read K1 as a machine,
K2 an item it makes, K3 a period. dreamor generates rows with `makeSet`, so rows always come out
in generation order, K1 by K1. "Are the rows sorted?" is therefore a question about the model's
structure, not about the data:

- **Which groupings are contiguous.** A constraint usually sums over one column and groups by the
  rest. The groups are runs only when the summed column is the innermost one. The example's two
  groupings, (machine, period) and (item, period), are both non-contiguous. The benchmark measures
  three shapes in every run: dropping the innermost column (runs), the middle one, and the leading one.
- **Whether ids line up with the order rows come in.** A hand-coder numbers entities as they are
  read, so outer ids follow the loops. Inner lists (an item's routings, say) come in whatever order
  the data has. This is factor A.

Rows per family range from 1e5 to 1e7. Above that, a model usually turns to another formulation,
such as column generation, which dreamor leaves out of scope. So the design covers that range as
a factor and does not extrapolate beyond it.

## Why no solver

Every arm creates the same variables and builds the same expressions, so the solver's share of
the work is the same in every arm and drops out of the comparison. `stub/` stands in for
Concert at the smallest constant cost a handle-over-heap-object design allows: a variable is a
16-byte heap object, an expression a vector of (variable, coefficient) that `end()` frees. (It is
not `tests/stub/`, which stores names and keeps expressions alive for the tests to inspect; that
would put most of the time and memory on the solver side.)

A real solver is left out on purpose, not for want of one. It is a black box, and what it costs
moves with its insides and the machine, not with the containers in front of it: CPLEX Concert
looks every variable's bounds up in a shared hash table, and how fast that goes falls off a cliff
once the caller runs a few dozen instructions between two variables, whichever arm it is. A ratio
measured through it would rank how a commercial library's internals happen to meet one CPU, not
the containers. `solver/` looks at real solvers
after the experiment, as a check on its results rather than part of them
([solver/README.md](solver/README.md)).

Reading the solution is not measured. It happens once per solve, and its cost disappears next to
the solve itself.

## The arms

| Arm | What it is | Groupings and sums |
|---|---|---|
| `dreamor` | `makeSet(...).where(keep).collect()`, F `VarTable`s on the set | `groupBy` per grouping; `sumOf(env, rows, vX)` or a column lambda; `slice()` for the driver set |
| `flat` | Hand-optimized: rows as ids and one variable vector per family, a `std::unordered_map` from the packed ids to the row (identity hash) | (K1, K2) groups as runs; other groupings compacted through an `unordered_map` on the packed key and laid out by counting sort (CSR); direct indexing; set operations two ways, hashed and merged |
| `map` | The usual hand-written code, as in `examples/production_planning_plain.hpp`: one `std::map<tuple<E1*, E2*, E3*>, IloNumVar>` per family, filled in one loop | Loops over the data with `keep` and a map lookup per variable; owner lists for the driver set. Reference cell only, with `--map`: it does not change with the library, so it belongs in a published run, not in a check |

`flat` is what a skilled hand-coder writes with the standard library when they know the model's
layout. It is allowed everything such a person knows:
- dense ids;
- rows in loop order;
- an identity hash on packed ids, which turns key order into bucket order;
- the innermost grouping as runs.

These advantages are kept on purpose. What they lean on, ids lining up with the order rows come
in, is factor A, so the analysis shows how much of the gap they account for.

Its containers stay the standard library's (`std::vector`, `std::unordered_map`,
`std::unordered_set`), with layouts such a person works out, such as CSR. A hand-written hash table
or set algebra would not be that hand-coder any more but a second set library, and the comparison
would no longer say what dreamor saves over writing the model by hand.

Set operations come two ways from the standard library, and every run measures both, one right
after the other:
- **hashed** (`set_*`): what comes to hand first, a `std::unordered_set` on the packed ids;
- **merged** (`set_*_merge`): the `<algorithm>` way. The packed keys are sorted with `std::sort`,
  then merged with `std::set_union`, `std::set_intersection` or `std::set_difference`. The
  projection uses `std::unique`, and the membership a `std::binary_search` into B's sorted keys.

A merged result comes out in key order, not in dreamor's first-appearance order. A hand-coder
often has no use for that order, so the merge stays a fair rival, and the checks do not depend on
order. dreamor has one way, and the report sets each of flat's against it. `set_*_best` sets the
faster of the two in each cell against it, since a hand-coder who knows the data picks one.

## Design

| Factor | Levels | What the higher levels exercise |
|---|---|---|
| N rows | 1e5 / 1e6 / 1e7 | cache-resident, the boundary, DRAM-bound |
| O K2 owner | shared / owned | K2s that belong to one K1: many small K2 groups |
| D density | dense / sparse | 10% of candidates kept, the predicate evaluated, 10x the K1s |
| S fan-out | uniform / skewed | Pareto-distributed list sizes with the same mean: uneven groups |
| A id order | aligned / inner / all | the K2/K3 lists shuffled and their ids permuted; then the K1 ids too |
| G group size | large / small | 5 K2s and 10 K3s per K1 instead of 50 and 100, same N: many small groups |
| F families | 1 / 3 | three variable families on the same set |

- **Size and runs.** By default the five 2-level factors run as a half fraction, F = O·D·S·G
  (defining relation I = ODSGF). That is resolution V: every main effect and two-factor
  interaction is aliased only with interactions of three or more factors, so all of them are
  still estimated. N and A are crossed in full: 3 × 16 × 3 = 144 cells, with dreamor and flat in
  every cell, × repetitions. With 2 repetitions that is 576 runs.
  - `--full` runs all 288 cells, twice as many runs, for when a three-factor
    interaction is in question.
  - The results folder says which design ran (`_half` / `_full`), so the two never resume into
    each other.
- **Rows are independent of A.** Every entity has a serial (its place in the data) and an id (the
  integer flat packs), and A permutes ids only. Every level of A therefore has the same rows,
  bounds and checks.
- **Pairing and blocks.** The same repetition number means the same seed and the same model, so
  the arms are compared pairwise, and a repetition is a block.
- **Other phases** besides the factorial:
  - `reference` (`--map`): map in the reference cell (the example's shape: shared, dense, uniform, aligned,
    large, F=3) at every N.
  - `--stress`: the reference cell at 3e7 rows, outside the analysis.

## Responses

Every run measures every phase, in this order, in a fresh process. A model build is a one-shot,
so every measurement is a cold one.

| Phase | What it is (the example's use) |
|---|---|
| `build` | rows + container + F families |
| `prep_contig` / `prep_skip` / `prep_lead` | groupings by (K1, K2), (K1, K3), (K2, K3): the innermost, middle and leading column dropped |
| `g_contig` | one expression per (K1, K2) group, the table as the term |
| `g_skip` | one expression per (K1, K3) group, `coef2(k2) * x` by columns (machine capacity) |
| `g_lead` | one expression per (K2, K3) group, the table as the term |
| `slice` | a driver set of (K2, K3) keys, with every pair the rows have and one absent pair per K2, one expression per key (stock balance) |
| `coef` | one expression over every row, `coef1(k1) * y` with y the last family (objective) |
| `cross` | every row's x and the y of the same row, found by the row (setup link) |
| `neighbor` | every row's neighbour (k1, k2, previous k3), looked up by its columns |
| `set_select` | the set projected on (K2, K3), first occurrences: `S.select<K2Col, K3Col>()` |
| `set_unite` / `set_intersect` / `set_difference` | A and B: A = `S.where(inA)`, B = a set generated apart, from the K3 lists moved one period on, partly overlapping A |
| `set_member` | `B.membership()` built, and the set filtered by it: `S.where(inB)` |
| `set_*_merge` | flat only, right after each `set_*`: the same operation on sorted keys (see the arms); dreamor's `set_*` is its counterpart |
| `set_*_best` | in the report only: flat's faster of `set_*` and `set_*_merge` in each cell |
| `hit_ns` / `miss_ns` | one random lookup of a present key / a membership test of an absent one |

- **Per phase**, a run records wall, user and system time and minor page faults
  (`<phase>_ms`, `_user_ms`, `_sys_ms`, `_minflt`). On a fresh process much of a build is the
  kernel handing out pages, and how expensive that is depends on the machine.
- **The scenario** is the sum of the phases the example's model build is made of: build,
  prep_skip, g_skip, prep_lead, slice, coef, cross and neighbor. It is the headline.
- **Set operations** are measured beside it: models reach for them, but the example does not,
  so they are responses of their own. Building A and B is not measured. flat runs each of them
  hashed and merged, and knows A has no repeats: what a hand-coder writes, not a set library of
  their own (see the arms). map does not run them.
- **Memory:** `container_bytes` (resident growth over build and groupings, minus the variables'
  own heap) and `peak_rss_bytes`.
- **Checks:** `groups` and `chk_<phase>` must be equal across arms for the same model. Each
  variable's bound is a tag derived from its row's serials and its family, and each check hashes
  the group's key. Equal checks therefore mean the same variables, with the same coefficients, in
  the same groups. A set operation's check sums its rows' hashes, whatever their order, and flat's
  `chk_set_*_merge` must equal the operation's `chk_set_*`.

## The machine

Core type is noise, not a factor, so one experiment stays on one core class. `--cpu auto` (the
default) picks one CPU as follows:

1. On Intel hybrid parts, the P-cores (`/sys/devices/cpu_core/cpus`).
2. Among those, the largest last-level cache. On a dual-CCD X3D part, that is the V-cache CCD, and
   the N=1e5-1e6 levels depend on it directly.
3. The highest ACPI CPPC `highest_perf`, or failing that the highest `cpuinfo_max_freq`. These are
   the preferred cores, which exist on homogeneous parts too.
4. The lowest-numbered physical core that is not CPU 0's, since CPU 0 takes the interrupts.

`--cpu N[,M]` pins to the CPUs you name; `--cpu none` does not pin. The meta file records the
choice and why, together with the governor, boost, SMT, transparent huge pages, `GLIBC_TUNABLES`
and the pinned CPU's last-level cache. Each run also records the CPU it started and ended on.

- **Under WSL or a VM**, `auto` does not pin, because pinning would pin a virtual CPU only. The
  runs still go ahead, with noisier timings that the random order and the blocks absorb.
  Settings the guest cannot see, such as the Windows power mode, are worth a look before a long
  run.
- **Root-only settings** (the governor, boost) are reported, not changed.
- **Machines are not compared.** Conclusions are drawn within one machine. The comparison is
  between arms on that machine, so a run under WSL2 is as valid as one on bare metal. The
  environment is stated with the result: the meta file records it, and the report's Summary sheet
  names it.

## Running

```sh
bench/build.sh                                         # needs only a C++20 compiler
cd bench
python3 run_doe.py --reps 2 --n-levels 100000          # quick trial: N = 1e5 only
python3 run_doe.py --reps 2 --map                      # the experiment
python3 make_report.py results/<run>/result.csv        # needs: pip install openpyxl
python3 -m unittest test_run_doe                       # the CPU choice (fake /sys trees), the design
```

- **Where results go.** Each run writes to its own folder, `results/<date>_<commit>_N<levels>_<half|full>/`
  (`result.csv`, `result_meta.json`, and `report.xlsx` from `make_report.py`). Uncommitted changes
  add `-dirty-<hash of the diff>` after the commit, so the name says exactly which code ran, and
  the meta file records the commit too. `results/` is ignored by git: it is a local log, kept or
  deleted at will. `--out` writes elsewhere.
- **The published result.** At a release, that release's run goes into `reference/`: its
  `result.csv` and `result_meta.json`, copied from its run folder, on whatever machine it ran - the
  meta file says which (CPU, virtualization, pinning). Earlier references stay in git history.
- **Memory.** A pilot runs the heaviest cell and the reference cell at two sizes each and fits
  bytes and seconds per row. Runs refuse to start if the heaviest cell's expected peak at the
  largest N exceeds 85% of available memory.
- **Resuming.** An interrupted run resumes when started again the same day with the same code and
  N levels (or with the same `--out`): finished runs are skipped, and the pilot is reused from
  `result_meta.json`.
- **Planning.** Every start prints a rough time estimate from the pilot: a range from every run
  at the reference cell's pace to every run at the heaviest cell's. It is rough - how long the
  experiment takes depends on the machine and on the cells - so read it as an order of magnitude.
  `--dry-run` prints the plan and the estimate only. `--cooldown-s` pauses before each run.
- **Recalculation.** Every summary in the workbook is a formula over the Raw sheet, so Excel
  recalculates on open. LibreOffice users: `soffice --headless --convert-to xlsx` writes the
  computed values.

## Reading the results

- **Ratios.** A ratio is flat / dreamor (map / dreamor in the reference cell), so above 1 means
  dreamor is faster or smaller.
- **Two headlines.**
  - The stage scenario, by N, F, A and D (Summary and Scenario sheets).
  - What moves the ratio (Effects sheet).

  A geometric mean over all cells is shown but is not the headline: it weighs every cell as if
  models were spread evenly over them.
- **Effects.** Effects are on ln(ratio), so a multiplier of 0.8 means that factor's high level
  makes the ratio 0.8 times as large.
  - A 2-level factor has one effect: the high level's mean minus the low level's.
  - A 3-level factor (N, A) has a linear effect (high minus low) and a quadratic one (the two ends'
    mean minus the middle).
  - Two-factor interactions use the linear parts.
  - Pure error comes from the repetitions within each cell.
- **Where the time goes.** For the reference cell, the Scenario sheet splits every phase into wall
  and system time.
- **Drift.** The Charts sheet plots each run's residual against the run order. A trend there means
  the machine changed during the run, for example by heating up.
- **Check.** The Check sheet must report no mismatches; if it does, the arms did not do the same
  work.

The benchmark is not part of CI: shared runners are too noisy for timing.
