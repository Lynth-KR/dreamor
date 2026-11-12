# Catalog results

The reference run of the catalog. `design.md` describes the families, their phases, the arms and the factors that
the tables name.

The reference run of every family on dreamor 0.9.0 ran on an AMD Ryzen 7 5700X with 32 MB of L3, on bare metal under
Linux 7.0. Boost was off, the governor was `performance` at 3.26 GHz, and the runner was pinned to one core.

- **Builds.** GCC 13.3 and Clang 18.1 built the binaries, both on libstdc++ 13, with `-O2 -DNDEBUG -march=native`.
- **Design.** Each family ran resolution VI: 32 cells of the six factors for each k and N. The levels of k were 2, 3,
  5 and 8, and also 1 in `lookup` and `make`. The levels of N were 1e5 and 1e6. That makes 256 cells, or 320 in
  `lookup` and `make`. The data family ran its own full design of 8 cells.
- **Repetitions.** Each cell ran 5 repetitions, each under a memory layout and a code layout of its own.
- **Runs.** Each compiler made 41,720 runs, with seed 20261025, the default of `run.py`. The `make` family ran again
  after the variables' names changed: dreamor's arm on the final code, and the hand-written arms' runs copied from
  the reference run, whose binaries they share (`run.py --reuse`), 640 of them run again as controls, at 0.999 /
  0.997 of their copies' time. No check mismatched.
- **Tests.** A ratio or an effect below differs from 1 when a two-sided test rejects a ratio of 1 at the 5% level,
  after Holm's adjustment over the tests of its table, for each family and compiler. The standard errors come from the
  variation between repetitions, as the intervals do. A ratio that does not differ is said to show no difference,
  which does not make it equal to 1.
- **Bounds.** A row marked **bound** in `lookup` is not a measurement. It is the ratio with the hand-written lookups
  written without their key array, estimated from measured speed-ups and taken from dreamor's side
  ([A note on the hand-written lookups](note-hand-lookups.md), "What it would change"). It has no interval and no
  test.

`report.py` writes the rest into each family's `summary.md`: every arm, the absolute times, the variation between
layouts, and the factors' effects.

A ratio below is a hand-written arm's time over dreamor's time, the geometric mean over the cells that the arm ran in.
Above 1, dreamor is faster. `best` is the fastest hand-written arm in each cell. The usual code is `std::map`, and
`std::set` in `setop`. Each entry shows GCC 13 / Clang 18. The 95% intervals come from the variation between
repetitions. They are within 2.2% of each ratio, except for the data family's `value`, at 3.5% / 2.5%, so the tables
leave them out.

Every measured ratio in the table below differs from 1, over each family's phases and comparisons, except `sum_lead` of
`group` under GCC, at 1.00. A ratio shown as 1.00 elsewhere, such as `split` of `slice`, differs by less than 0.5%. Of
the arms that the table leaves out, two also showed no difference: `array` on `project` of `lookup`, on both compilers,
and `sort` on `coef` of `slice` under GCC.

| Family | Phase | best | usual code |
|---|---|---|---|
| lookup | build | 1.40 / 1.43 | 2.66 / 3.11 |
| | index | 4.90 / 4.57 | - |
| | neighbor | 1.22 / 1.25 | 2.56 / 2.44 |
| | project | 1.07 / 1.45 | 4.02 / 6.42 |
| | hit | 1.22 / 3.45 | 6.90 / 6.44 |
| | **hit, bound** | **1.07 / 0.94** | - |
| | miss | 1.69 / 3.97 | 12.44 / 12.14 |
| | **miss, bound** | **1.46 / 1.50** | - |
| | half | 1.07 / 2.66 | 5.76 / 5.65 |
| | **half, bound** | **0.97 / 0.95** | - |
| | **total** | **1.50 / 3.01** | **6.17 / 5.97** |
| | **total, bound** | **1.39 / 1.35** | - |
| group | build | 0.77 / 0.76 | 0.90 / 0.91 |
| | prep_inner / sum_inner | 1.03 / 1.09, 1.01 / 1.07 | 5.48 / 5.67, 1.08 / 1.15 |
| | prep_lead / sum_lead | 1.20 / 1.18, 1.00 / 1.06 | 3.23 / 3.24, 1.06 / 1.12 |
| | prep_mid / sum_mid | 1.16 / 1.14, 1.04 / 1.06 | 3.03 / 3.00, 1.11 / 1.14 |
| | sum_where | 1.06 / 1.12 | 1.10 / 1.17 |
| | **total** | **1.12 / 1.15** | **2.13 / 2.15** |
| setop | build | 0.77 / 0.76 | 0.77 / 0.76 |
| | select | 1.03 / 0.93 | 3.42 / 2.95 |
| | unite | 1.54 / 1.44 | 4.07 / 3.56 |
| | intersect | 1.62 / 1.52 | 5.27 / 4.54 |
| | difference | 1.80 / 1.71 | 5.52 / 4.65 |
| | member | 1.90 / 1.67 | 4.00 / 3.31 |
| | **total** | **1.68 / 1.54** | **4.32 / 3.72** |
| slice | build | 0.87 / 0.87 | 1.01 / 1.03 |
| | prep_inner / slice_inner | 1.42 / 1.36, 1.03 / 1.04 | 5.68 / 5.68, 1.22 / 1.20 |
| | split / coef | 1.00 / 1.00, 0.91 / 0.87 | 1.11 / 1.10, 1.06 / 0.98 |
| | prep_lead / slice_lead | 1.18 / 1.20, 1.05 / 1.09 | 3.04 / 2.92, 1.35 / 1.34 |
| | **total** | **1.06 / 1.08** | **1.73 / 1.68** |
| make | collect | 1.05 / 1.06 | 1.06 / 1.07 |
| | vars / vars_named | 1.06 / 1.13, 1.12 / 1.14 | 1.07 / 1.15, 1.14 / 1.15 |
| | join | 1.31 / 1.31 | 1.62 / 1.92 |
| | **total** | **1.14 / 1.16** | **1.26 / 1.40** |
| order | build | 0.86 / 0.86 | 2.04 / 2.13 |
| | index | 4.20 / 3.89 | - |
| | step | 0.26 / 0.35 | 0.43 / 0.49 |
| | lag | 1.37 / 1.55 | 1.96 / 1.92 |
| | window | 0.75 / 0.80 | 1.65 / 1.79 |
| | **total** | **1.03 / 1.14** | **1.58 / 1.66** |
| data | build | 0.97 / 1.02 | 7.48 / 12.17 |
| | load | 2.24 / 2.14 | 5.85 / 5.74 |
| | find_hit / find_miss | 2.01 / 1.65, 1.38 / 1.15 | 19.90 / 16.15, 17.45 / 14.46 |
| | solution | 0.40 / 0.40 | 8.76 / 14.96 |
| | value | 0.12 / 0.26 | 54.46 / 63.51 |
| | **total** | **1.68 / 1.59** | **11.99 / 11.51** |

The ratio of `best` over each family's total, by k. Every measured ratio of this table differs from 1, over each
family's levels of k:

| Family | k = 1 | k = 2 | k = 3 | k = 5 | k = 8 |
|---|---|---|---|---|---|
| lookup | 0.91 / 3.67 | 1.19 / 2.66 | 1.67 / 3.16 | 1.75 / 2.68 | 2.35 / 2.97 |
| **lookup, bound** | **0.75 / 0.86** | **1.11 / 1.17** | **1.62 / 1.51** | **1.67 / 1.48** | **2.27 / 2.02** |
| group | - | 1.13 / 1.19 | 1.11 / 1.13 | 1.07 / 1.10 | 1.17 / 1.17 |
| setop | - | 2.38 / 2.30 | 1.80 / 1.68 | 1.38 / 1.26 | 1.33 / 1.17 |
| slice | - | 1.09 / 1.07 | 1.06 / 1.10 | 1.01 / 1.04 | 1.08 / 1.12 |
| make | 1.08 / 1.08 | 1.53 / 1.60 | 1.08 / 1.11 | 1.05 / 1.06 | 1.01 / 1.03 |
| order | - | 0.87 / 1.05 | 1.30 / 1.41 | 1.08 / 1.14 | 0.92 / 0.98 |

Coverage: dreamor and the usual code ran every cell. Packed keys did not fit in 64 bits in at least one repetition of 36 cells of
`lookup`, `group`, `setop` and `order`, and of 28 of `slice`: 28 of the 64 cells at k = 8 (24 in `slice`), and the 8 cells at
k = 5 with owned middle columns and small lists (4 in `slice`). The direct array fit in 96 of the 320 cells of
`lookup`, the 64 at k = 1 and 32 at k = 2, and in 32 of the 256 cells of `order`, all at k = 2.

- **Against the usual code.** dreamor was faster in every family's total on both compilers, by 1.26 to 12.0 times. On
  lookups of absent keys, it was faster by 12.1 to 12.4 times.
- **Against the best hand-written arm.** dreamor was ahead in every family's total on both compilers: lookup 1.50 / 3.01
  (bound 1.39 / 1.35), setop 1.68 / 1.54, data 1.68 / 1.59, group 1.12 / 1.15, order 1.03 / 1.14, slice 1.06 / 1.08, and
  make 1.14 / 1.16. A hash map or a sorted array that the hand-coder picks for each cell did not beat dreamor overall.
- **By k.** Under GCC, the lead of `lookup` grew with the columns, from 1.19 at k = 2 to 2.35 at k = 8. The leads of
  `group` and `slice` held at 1.01 to 1.19 at every k. The lead of `setop` narrowed, from 2.38 to 1.33 under GCC and
  from 2.30 to 1.17 under Clang. In `make`, dreamor was ahead at every k on both, at 1.01 to 1.60. In `order`, the
  best arm was ahead at k = 2 under GCC, at 0.87, and at k = 8 on both, at 0.92 / 0.98.
- **At k = 1.** The 0.91 of `lookup` under GCC is the direct array, which fit every cell at k = 1. It was ahead on
  hits, at 0.82, and on `half`, at 0.81, and behind on misses, at 1.11. Under Clang, the array took 3.67 times
  dreamor's time. Against the hash map over the k1's id, `identity`, dreamor was 2.92 / 7.44 over the family: 1.83 /
  6.69 on hits, 5.22 / 16.85 on misses, and 1.84 / 5.50 on `half`.
- **The sums of `group`** were ahead of the best arm at 1.01 to 1.06 under GCC, where `sum_lead` showed no difference,
  and at 1.06 to 1.12 under Clang.
- **Where hand-written code won, it knew the data's structure.**
  - `build` in `group`, `setop`, `slice` and `order`, at 0.76 to 0.87: the hand-written arms keep ids or positions,
    and dreamor keeps a set of rows that it can index.
  - `step` at 0.26 / 0.35 and `window` at 0.75 / 0.80: the hand-written arms keep a position beside each entity and
    do arithmetic on it, and dreamor looks the period up in `Ordered`.
  - `select` under Clang, at 0.93: the hand-written arm sorts packed integers and drops the repeats, and dreamor
    hashes tuples. Under GCC, dreamor was ahead, at 1.03.
  - `solution` at 0.40 / 0.40 and `value` at 0.12 / 0.26: the hand-written arm reads the solver's own array by the
    entity's place. dreamor copies the values into a table once for each solve, and works out an entity's row from
    its address.
  - The `coef` phase of `slice`, at 0.91 / 0.87: the term reads a coefficient by the row's columns. In dreamor that
    is a lookup that checks all k columns against the row that `sumExpr` published. At k = 8, the phase ran 1.01 to
    1.20 times the instructions of the hand-written arms under GCC, and 1.17 to 1.39 times under Clang. The
    hand-written arm reads the coefficient at the row's position.
- **The direct array, where it fit,** was the fastest hand-written arm in both families. It took 0.47 / 0.59 of
  dreamor's time in `order`, and 0.80 in `lookup` under GCC. Under Clang, the array in `lookup` took 2.80 times
  dreamor's time.
- **The two compilers agreed on dreamor, and the hand-written lookups moved.** Over the same cell and repetition, the
  time under Clang 18 over the time under GCC 13 was 0.91 to 1.07 for dreamor, by family. The direct array of `lookup`
  took 3.70 times its time under GCC, and the two packed-key arms took 1.90 and 1.59 times. Hence the `best` of
  `lookup` was 3.01 under Clang and 1.50 under GCC. Their instructions moved by 0.93 to 1.19 times, so the time went
  to waiting, not to more work. Why, and what the same lookups cost written otherwise, is in
  [A note on the hand-written lookups](note-hand-lookups.md).
- **The factors acted the same under both compilers.** Of the 149 effects past 5% that differed from 1 on either
  compiler, 146 moved the gap to the best arm the same way under both compilers. Half of them were within 2.0% of
  each other, such as the sparse rows (D) of `order`, at 1.86 / 1.80 on the total, and the permuted ids (I) of
  `setop`, at 1.56 / 1.57. Of the three that differed, two were the sparse rows on `build`: in `group` at 1.07 / 0.97,
  and in `slice` at 1.01 / 0.93. There each compiler compiled one side tighter: in sparse cells, Clang ran the
  hand-written arms in 0.83 to 0.89 of GCC's time, on 0.83 to 0.87 of its instructions, and dreamor in 0.85 to 0.95;
  in dense cells, dreamor in 0.89 to 0.96 and the hand-written arms in 0.95 to 0.98. The third was random access on
  `find_hit` of the data family, at 1.05 / 0.92.
