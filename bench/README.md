# Container benchmark

How much does dreamor cost, or save, compared with writing the same model code by hand? This
benchmark measures it without a solver, as a designed experiment: three implementations of the
same indexed variable family, the same rows, the same variables, the same expressions.

## Why no solver

Every arm creates the same variables and builds the same expressions, so the solver's share of
the work is the same in every arm and cancels out of the comparison. `stub/` stands in for
Concert at the smallest constant cost a handle-over-heap-object design allows: a variable is a
16-byte heap object, an expression a vector of variable pointers that `end()` frees. (It is not
`tests/stub/`, which stores names and keeps expressions alive for the tests to inspect; that would
put most of the time and memory on the solver side.)

Reading the solution is not measured. It happens once per solve, and its cost disappears next to
the solve itself.

## The arms

| Arm | What it is | Group sums |
|---|---|---|
| `dreamor` | `makeSet(...).where(keep).collect()`, one `VarTable`, `groupBy` per grouping | `sumOf(env, rows, vX).build()` |
| `map` | The usual hand-written code, as in `examples/production_planning_plain.hpp`: `std::map<tuple<E1*, E2*, E3*>, IloNumVar>` filled by nested loops over the data | Loop over the data again, test `keep`, look each variable up. For the K2 and K3 groupings, owner lists (which K1s use this K2 / K3) are built first so that no grouping scans all the data |
| `flat` | Hand-optimized: rows and variables in parallel vectors, a `std::unordered_map` from a packed integer key to the row, CSR per grouping (counting sort) | Slice the CSR, index the variables directly |

`flat` is the reference for what direct indexing achieves. `map` is the reference for what model
code without index sets usually looks like. Each arm takes its own natural route to the same
result. For example, `map` re-tests the `keep` predicate while summing, because without a
materialized set the predicate lives in the loops; dreamor evaluated it once, when the set was
built.

## Model and factors

Rows are `{(k1, k2, k3) : k2 in C2(k1), k3 in C3(k1), keep(k1, k2, k3)}`, one variable per row,
K1s added until there are N rows. On average there are 50 K2s and 100 K3s per K1.

| Factor | Low | High | What the high level exercises |
|---|---|---|---|
| W workload | `prefix`: group by K1 | `mixed`: group by K1, K2 and K3 | groupings by keys other than the first |
| O K2 owner | `shared`: K2s from a common pool | `owned`: each K2 belongs to one K1 | many small K2 groups |
| D density | `dense`: every candidate | `sparse`: 10% of candidates | a sparse set, predicate evaluation, 10x the K1s |
| S fan-out | `uniform` | `skewed`: same mean, Pareto | uneven group sizes |

The design is a 2^4 full factorial (16 cells) × arms × repetitions. A half fraction would be
resolution IV, which confounds two-factor interactions with each other, so there is no `--half`.

The same repetition number means the same seed and the same model, so the arms are compared
pairwise. A scale ladder (N/16 … N) in two reference cells supports extrapolation to larger N.
Every run is a fresh process, in a seeded random order.

Responses, per run:
- `build_ms`: rows, container and variables.
- `prep_ms`: the groupings.
- `g1_ms`, `g2_ms`, `g3_ms`: one expression per group.
- `traverse_ms`.
- `hit_ns`: one lookup of a present key. `miss_ns`: one membership test of an absent key. dreamor's
  `vX(row)` throws on an absent key, so the miss test uses `contains()`.
- `container_bytes`: resident growth over build and groupings, minus the variables' own heap.
- `peak_rss_bytes`.

`groups`, `check_groups` and `check_traverse` must be equal across arms for the same model. Every
variable carries a tag derived from its key, so equal checks mean the arms handed out the same
variables in the same groups.

## Running

```sh
bench/build.sh                                       # needs only a C++20 compiler
cd bench
python3 run_doe.py --budget-gb 0.15 --reps 2         # quick trial, a few minutes
python3 run_doe.py --budget-gb 3.5 --reps 3          # the experiment
python3 make_report.py results/<run>/result.csv      # needs: pip install openpyxl
```

- **Where results go.** Each run writes to its own folder, `results/<date>_<commit>_<budget>gb/`
  (`result.csv`, `result_meta.json`, and `report.xlsx` from `make_report.py`). Uncommitted changes
  add `-dirty-<hash of the diff>` after the commit, so the name says exactly which code ran, and
  the meta file records the commit too. `results/` is ignored by git: it is a local log, kept or
  deleted at will. `--out` writes elsewhere.
- **The published result.** `reference/` holds one run, the one for the current release. Refresh
  it at a release by copying that release's run folder's `result.csv` and `result_meta.json`
  there; earlier references stay in git history.

- **Choosing N.** `--budget-gb` is the memory the heaviest arm may use. A pilot run turns it into
  the row count N. Runs refuse to start if the expected peak exceeds 85% of available memory.
- **Resuming.** An interrupted run resumes when started again the same day with the same code and
  budget (or with the same `--out`): finished runs are skipped, and N is reused from
  `result_meta.json`.
- **Planning.** `--dry-run` prints the plan and an upper bound on the time.
  `--impls dreamor,flat` limits the arms.
- **Recalculation.** Every summary in the workbook is a formula over the Raw sheet, so Excel
  recalculates on open. LibreOffice users: `soffice --headless --convert-to xlsx` writes the
  computed values.

## Reading the results

- **Ratios.** A ratio is other arm / dreamor, so above 1 means dreamor is faster or smaller. The
  Summary sheet shows the geometric mean over the 16 cells and the range.
- **Effects.** The Effects sheet shows how each factor moves that ratio. Pure error comes from the
  repetitions within each cell.
- **Check.** The Check sheet must report no mismatches; if it does, the arms did not do the same
  work.
- **Cache size.** N should be large enough that the containers do not fit in the CPU cache.
  Cache-resident runs flatter whichever arm has the most indirection to hide. The Scale sheet
  flags extrapolations whose smallest fitted rung is under 4x the last-level cache.

The benchmark is not part of CI: shared runners are too noisy for timing.
