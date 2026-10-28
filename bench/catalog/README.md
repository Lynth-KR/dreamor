# Catalog

What each of dreamor's operations costs against the same operation written by hand, over the
shapes data takes and the ways models reach it - not over one model's build. A whole model's build
fixes the operations, the order they run in and the keys they look up; the scenarios
(`../scenario/`) measure those. The catalog takes the operations one family at a time instead, and
the factors are the data's shape and the access pattern, so a result says which shape and pattern
it holds for.

## What is measured

A **family** is a group of operations on the same data, measured as phases of one process:

| Family | Phases |
|---|---|
| `lookup` | `build` the rows, a variable family x on them, and y on the (k1, period) pairs they have; `index` what the lookups go through; `neighbor` every row's x at the period before; `project` every row's y, a family on fewer columns; `hit` x at keys drawn at random; `miss` `contains()` of absent keys; `half` `contains()`, then x where it is there, over keys of which half are rows |
| `group` | `build` the rows and x; for each shape - `inner` (every column but the period: the groups are runs), `lead` (every column but k1), `mid` (every column but the second, k >= 3) - `prep_<shape>` the grouping and `sum_<shape>` one expression a group, x over its rows; `sum_where` one expression an inner group over its rows in even periods |
| `setop` | `build` the rows; `select` the rows projected on every column but k1; `unite`, `intersect`, `difference` of A (the rows a predicate on (k1, period) keeps) and B (generated apart, from the periods moved one on, overlapping A in part); `member` the rows B has, through its membership. A and B are not measured |
| `slice` | `build` the rows and x; for each shape - `inner`, `lead`, as in `group` - `prep_<shape>` the grouping and `slice_<shape>` one expression a key of a driver set, x over its group's rows; the driver holds every key of the shape and, beside each, one no row has (half the slices are empty, as some (item, period) pairs have no production); on the inner shape `split` (two expressions a key, even and odd periods, in one pass) and `coef` (c(t) * x a row, the term a callable on the row's columns) |
| `make` | what the others measure together as `build`, apart: `collect` the rows (makeSet), `vars` a variable a row, `vars_named` the same named `x_<id>_...`, and `join` the same rows again as A (every column but the period) joined with B (k1 and the period) on k1 |
| `order` | `build` the rows and x; `index` the row index and the periods' `Ordered`; `step` every row's period before, no variable; `lag` every row's x four periods before; `window` every row's x over the four periods up to its own |
| `data` | entities, not rows: `load` N entities one at a time (no key twice), `find_hit` each by its key, `find_miss` as many keys no entity has, `build` a variable an entity, `solution` every value as a solver hands them back, `value` each entity's value |

`total` in the report is every phase but `build`, summed.

**Arms.** dreamor against hand-written arms, written as a hand-coder writes them with the standard
library. Most pack the ids into one integer, column 0 most significant; one per family is the usual
code, on the entities themselves:

| Family | Arm | What it is |
|---|---|---|
| `lookup` | `identity` | packed key, `std::unordered_map` with libstdc++'s identity hash on integers |
| | `mixed` | packed key, `std::unordered_map` with a mixed hash (splitmix64) |
| | `array` | packed key, a direct array over every combination of ids, while it takes at most 16 slots a row |
| | `map` | `std::map` from the row's entities to its variable, filled as the rows are generated (as `examples/production_planning_plain.hpp` does); no index of its own, so its `index` phase is empty |
| `group` | `identity`, `mixed` | the group's packed key to its number through `std::unordered_map`, the rows counted into place (CSR) |
| | `sort` | (key, row) pairs sorted, the groups read off as runs |
| | `map` | `std::map` from the group's entities to its rows |
| `setop` | `identity`, `mixed` | `std::unordered_set` of packed keys |
| | `merge` | sorted keys through `<algorithm>`: `std::unique`, `std::set_union`, `std::set_intersection`, `std::set_difference`, `std::binary_search` |
| | `set` | `std::set` of the rows' entities |
| `slice` | `identity`, `mixed`, `sort`, `map` | the groupings of `group`, and what a key goes through: the group number in the `std::unordered_map`, `std::lower_bound` over the sorted keys, `std::map::find` |
| `make` | `identity`, `mixed`, `sort`, `map` | nested loops into a `std::vector`, a variable a row, names through `std::to_chars` - the same in every arm - and the join: B's periods by k1 through `std::unordered_map`, B sorted and searched with `std::equal_range`, a `std::map` from k1 to its periods |
| `order` | `identity`, `mixed`, `array`, `map` | as in `lookup`, with the period's position kept beside the entity and arithmetic on it in place of `Ordered` |
| `data` | `umap` | a `std::vector` of entities and a `std::unordered_map` from the key to its place; variables in an `IloNumVarArray`, values in an `IloNumArray`, both by that place |
| | `map` | `std::map`s by the key |

In `group`, the packed arms take the inner shape's groups as runs, as a hand-coder who knows the
generation order would; dreamor finds that out for itself.

Each cell is compared with every hand-written arm that runs in it and with the fastest of them
(`best`): a hand-coder who knows the data picks one, and which one wins is part of the result.

An arm that cannot run a cell is **unsupported** there, and the report counts it as a result
(Coverage) rather than a gap to fill: the array where the ids' product does not fit in memory, the
packed keys where it does not fit in 64 bits. Filling those cells with wider integers or a hash
written for the purpose would be designing the hand-coder's code for them, and the comparison would
no longer be with what a hand-coder writes. `map` runs everywhere, so `best` is the usual code
where packing does not apply.

**Factors.**

| Factor | Levels | What the higher level exercises |
|---|---|---|
| k | 2 / 3 / 5 | columns: k1, then k-2 columns listed by k1, then the period |
| N | 1e5 / 1e6 | rows |
| O | shared / owned | the middle columns' entities belong to one k1 each |
| D | dense / sparse | one candidate in ten is a row, by a predicate; ten times the k1s |
| S | uniform / skewed | Pareto list sizes with the same mean |
| G | large / small | lists of 100 periods and 50 middle combinations, or 10 and 5 |
| L | ordered / shuffled | the lists in a random order, so the rows come in that order |
| I | aligned / permuted | every column's ids in a random order |

L and I are the two halves of the container benchmark's A: L changes the order rows come in, which
every arm walks; I changes only the integers the hand-written arms pack.

`data` has entities, not rows, so none of the above but N applies. Its own two-level factors run
in full, 8 cells: `key`, an integer id or a 13-character code (`ITEM000012345`), and `access`, the
lookups in the order the entities were loaded in or in a random one.

## Why this way

- **Screening first.** The six two-level factors run as a 2^(6-2) fraction of resolution IV, 16
  cells (L = ODS, I = DSG; defining relation I = ODSL = DSGI = OGLI): every main effect is clear of
  every two-factor interaction, and two-factor interactions are aliased with each other. That
  ranks the factors for a fraction's price, as screening designs do (Jain, *The Art of Computer
  Systems Performance Analysis*, 1991, ch. 19; NIST/SEMATECH e-Handbook of Statistical Methods,
  5.3.3). Times enter on a log scale, so effects are ratios. `--resolution 6` runs the half fraction I = ODSGLI, 32
  cells, where two-factor interactions are clear too; it is not an extension of the 16 (adding
  quarters to a resolution IV fraction keeps a word of length 4), but half of the 16 are among its
  32.
- **k and N in full.** Both have more than two levels or change the memory regime, so they are
  crossed with the fraction rather than aliased.
- **One process a run, the same setup in every arm.** The data and the lookup samples are built
  before any phase, the same way for every arm, and the process exits without freeing them: the
  heap an arm's phases start from does not depend on the arm.
- **Three kinds of conditions.** The factors above are varied on purpose and reported as effects.
  The memory layout a run lands in - where the stack starts, which holes the heap has, where a hot
  loop falls against a cache line - is the operating environment: the arm does not choose it and a
  real program does not fix it. Timer resolution, frequency changes and other processes are
  measurement noise, kept down and recorded.
- **The layout is sampled, not fixed.** How an arm responds to layout is the arm's own: one that
  is less sensitive is the better design. What is not the arm's is which layout one run happens to
  draw, and every arm, being its own binary in its own process, draws its own. Under one fixed
  setup, a difference between arms is therefore confounded with the draws they got (Mytkowicz et
  al., ASPLOS 2009, doi:10.1145/1508244.1508275; Curtsinger and Berger, ASPLOS 2013,
  doi:10.1145/2451116.2451141). So each repetition runs under a layout setup of its own: an
  environment padding of its own length (0-4095 bytes, which moves the stack) and a heap seed of
  its own, from which blocks of random sizes are allocated and a random half freed before anything
  is built, since a fresh heap is an allocator's best case (van Kempen and Berger, ISMM '26,
  arXiv:2605.17119). Every arm of a repetition gets the same setup, and ASLR stays on. A ratio is
  then an expectation over layouts, and the report gives each arm's variation between layouts
  beside it, as a property of the arm. How fragmented the heap starts is a choice: real model
  code reads its data before it builds, so the heap is not fresh, but how far from fresh is not
  known; half of 65,536 blocks of 16-1023 bytes is an assumption, not a measurement.
- **Repetitions.** Screening runs 2; a run a conclusion rests on runs 5, the fewest from which a
  variance can be estimated (Kalibera and Jones, ISMM 2013, doi:10.1145/2464157.2464160).
- **Ratios with their base.** The report gives every ratio with a 95% interval and the absolute
  times beside it (Hoefler and Belli, SC15, doi:10.1145/2807591.2807644).
- **Checks.** Each phase folds what it handed out - the upper bounds the variables carry, which
  are tags of their rows' serials - into one number, and every arm must agree on it.

## Running

```sh
./build.sh
python3 run.py --family lookup --reps 2                       # N = 1e5, 1e6: a few minutes
python3 run.py --family lookup --n-levels 100000 --reps 1     # quick trial
python3 run.py --family lookup --resolution 6 --reps 2        # 32 cells
python3 report.py ../results/<run>                            # summary.md and cells.csv beside the runs
```

- `--pmu` takes hardware counters, comma-separated: `cycles`, `instructions`, `l1d_miss`,
  `dtlb_miss`, `branch_miss`, or `name=0x<config>` for a raw event (Zen 3's demand fills from L2,
  L3 and DRAM: `0x0843`, `0x0243`, `0x0143`). They form one group, counted in user mode, and show in
  each phase of `runs.jsonl`. The default, `auto`, counts `instructions` wherever the machine lets a
  process count its own (not on CI, nor in most containers) and says which in `meta.json`; `none`
  counts nothing.
- Results go to `../results/<date>_<commit>_catalog_<family>_N<levels>_res<4|6>/`: `runs.jsonl` (a
  run a line) and `meta.json` (design, machine, commit). Running the same thing again resumes it.
- `--phases a,b` measures those phases only, for a run that looks at a few: the phases they stand
  on (`build`, `index`, `prep_*`) still run, unreported. Every run also reports its setup stages -
  `heap`, `data`, and in `lookup` `samples` - in `setup_ms`, outside every ratio.
- The lookup keys are the rows, walked once and shuffled: each key a row drawn uniformly, each row
  at most once, whatever the lists' sizes.
- `--top-reps` sets the repetitions at the largest N; `--layout off` runs every repetition under
  the same layout.
- `--builds base=<path>,head=<path>` compares builds of `./catalog`, say the library before and
  after a change: every run of the plan runs once per build, one right after the other in a random
  order, under the same seed and layout. `python3 compare.py ../results/<run>` gives the later
  build's time over the earlier one's per family, phase and N, with an interval over cells, the
  cells that moved, and any result that differs between the builds; with counters, each one's
  ratio beside the time. Instructions barely move with the cache or the heap: a time ratio they do
  not follow is memory, one they follow is the code the compiler made. A phase's time includes what
  the phases before it left in the heap, so a phase whose code did not change can move too:
  measure it alone with `--phases` before reading that as an effect.
- The machine: a result a conclusion rests on comes from bare metal with the CPU's frequency fixed
  (boost off, the `performance` governor) and the runner pinned to one core, as the results below
  are. A run under WSL2 or another virtual machine is not pinned and screens only.

## Results

The reference run of every family on dreamor 0.3.0 (the library at `52f5cc9`): AMD Ryzen 7 5700X
(32 MB L3), bare metal, Linux 7.0, boost off and the `performance` governor (3.25 GHz), pinned to
one core; built with GCC 13.3 and with Clang 18.1, `-O2 -march=native`; resolution VI (32 cells of
the six factors, by k 2, 3, 5 and N 1e5, 1e6: 192 cells; data's own full design, 8), 5 repetitions,
a layout each; 28,920 runs per compiler, no check mismatch. Each family's `summary.md` holds the rest:
every arm, absolute times, the variation between layouts, the factors' effects.

A ratio is a hand-written arm's time over dreamor's, the geometric mean over the cells the arm ran in;
above 1, dreamor is faster. `best` is the fastest hand-written arm in each cell; the usual code is
`std::map` (`std::set` in setop). Each cell shows GCC 13 / Clang 18. The 95% intervals, from the
variation between repetitions, are within 4% of each ratio (data under Clang, with 8 cells: 6%), so
they are left out here.

| Family | Phase | best | usual code |
|---|---|---|---|
| lookup | build | 1.41 / 1.44 | 2.57 / 2.81 |
| | index | 5.53 / 5.91 | - |
| | neighbor | 1.11 / 1.20 | 2.97 / 2.96 |
| | project | 1.07 / 1.18 | 5.88 / 6.88 |
| | hit | 1.14 / 3.32 | 6.58 / 5.77 |
| | miss | 1.59 / 3.52 | 14.31 / 13.53 |
| | half | 1.01 / 2.58 | 5.73 / 5.83 |
| | **total** | **1.50 / 2.85** | **5.94 / 5.79** |
| group | build | 0.80 / 0.78 | 0.93 / 0.93 |
| | prep_inner / sum_inner | 0.82 / 0.89, 0.90 / 0.93 | 6.22 / 5.76, 1.03 / 1.05 |
| | prep_lead / sum_lead | 1.19 / 1.23, 0.90 / 0.96 | 3.79 / 3.54, 0.98 / 1.04 |
| | prep_mid / sum_mid | 1.11 / 1.16, 0.95 / 0.98 | 3.71 / 3.40, 1.05 / 1.08 |
| | sum_where | 0.96 / 1.01 | 1.04 / 1.08 |
| | **total** | **1.02 / 1.07** | **2.28 / 2.18** |
| setop | build | 0.87 / 0.81 | 0.87 / 0.81 |
| | select | 0.94 / 0.88 | 3.86 / 3.49 |
| | unite | 1.52 / 1.56 | 5.68 / 5.75 |
| | intersect | 1.63 / 1.62 | 7.28 / 7.14 |
| | difference | 1.94 / 1.96 | 7.72 / 7.52 |
| | member | 2.06 / 1.94 | 5.38 / 4.86 |
| | **total** | **1.78 / 1.75** | **5.83 / 5.61** |
| slice | build | 0.86 / 0.83 | 1.00 / 0.99 |
| | prep_inner / slice_inner | 1.25 / 1.23, 0.98 / 0.96 | 5.93 / 5.81, 1.23 / 1.20 |
| | split / coef | 0.98 / 0.97, 0.96 / 0.89 | 1.15 / 1.13, 1.18 / 1.09 |
| | prep_lead / slice_lead | 1.21 / 1.26, 0.91 / 0.96 | 3.42 / 3.33, 1.17 / 1.21 |
| | **total** | **1.06 / 1.05** | **1.83 / 1.76** |
| make | collect | 1.07 / 1.05 | 1.08 / 1.06 |
| | vars / vars_named | 1.09 / 1.22, 0.89 / 1.06 | 1.10 / 1.23, 0.90 / 1.07 |
| | join | 1.42 / 1.43 | 1.77 / 1.97 |
| | **total** | **1.13 / 1.21** | **1.27 / 1.45** |
| order | build | 0.82 / 0.80 | 2.38 / 2.40 |
| | index | 3.86 / 4.16 | - |
| | step | 0.19 / 0.25 | 0.32 / 0.35 |
| | lag | 1.51 / 1.69 | 2.41 / 2.32 |
| | window | 0.77 / 0.76 | 1.85 / 2.12 |
| | **total** | **1.09 / 1.17** | **1.78 / 1.96** |
| data | build | 0.90 / 0.92 | 6.92 / 10.97 |
| | load | 2.30 / 2.25 | 5.66 / 6.02 |
| | find_hit / find_miss | 1.10 / 0.97, 1.23 / 0.92 | 11.37 / 9.48, 15.72 / 11.56 |
| | solution | 0.39 / 0.40 | 8.64 / 14.78 |
| | value | 0.06 / 0.08 | 26.73 / 20.21 |
| | **total** | **1.42 / 1.32** | **9.88 / 9.56** |

Coverage: dreamor and the usual code run every cell. Packed keys do not fit in 64 bits in up to 8
cells of a family (k = 5, owned middle columns, small lists), and the direct array fits in 32 of 192
(lookup, order).

- **Against the usual code**, dreamor is faster in every family's total on both compilers: 1.3 to
  9.9 times, and 13.5-14 times on lookups of absent keys.
- **Against the best hand-written arm**, it is ahead in every family's total on both compilers:
  setop 1.78 / 1.75, lookup 1.50 / 2.85, data 1.42 / 1.32, make 1.13 / 1.21, order 1.09 / 1.17, and
  slice and group by 2-7%. A hash map or a sorted array that the hand-coder picks per cell does not
  beat it overall.
- **Where hand-written code wins, it knows the data's structure.** `build` in group, setop, slice and
  order (0.78-0.87): the hand-written arms keep ids or positions, dreamor a set of rows it can
  index. `prep_inner` (0.82 / 0.89): the hand-coder knows the inner groups are runs. `step` and
  `window` (0.19-0.25, 0.76-0.77): a position kept beside each entity, and arithmetic on it, against
  `Ordered`'s lookup. `select` (0.94 / 0.88): sorting packed integers and dropping repeats, against a
  hash of tuples. `solution` and `value` (0.39-0.40, 0.06-0.08): values in an array by the entity's
  place, against a table keyed by the entity. `vars_named` under GCC (0.89): a name written straight
  into a buffer.
- **The direct array, where it fits** (32 cells of lookup and order), is the fastest arm in order -
  0.49 / 0.53 of dreamor's time - and in lookup under GCC (0.62). Under Clang lookup's array takes
  1.61 times dreamor's time.
- **The two compilers agree on dreamor; the hand-written lookups are what moves.** Clang 18 over
  GCC 13, the same cell and repetition: dreamor's time is 0.89-1.02 by family, while lookup's direct
  array takes 2.60 times its GCC time and the two packed-key arms 1.80 and 1.51 - hence lookup's
  best at 2.85 under Clang against 1.50 under GCC. Their instructions barely move (0.9-1.4 times in
  the phases that look keys up), at every N and every factor level alike: a fixed stall per key. The
  hand-written arms collect a key's ids in a `std::array` returned by value, and Clang copies it with
  one 16-byte load over the 4-byte stores just made, which the store buffer cannot forward; GCC reads
  each id where it was stored. Which factors widen or narrow the gap to the best arm is the same
  under both: of the effects past 5% (Holm-adjusted, per family), all but one move the gap the same
  way, at nearly the same size - setop's permuted ids (I) 1.68 / 1.69, order's sparse rows (D)
  1.90 / 1.99.
