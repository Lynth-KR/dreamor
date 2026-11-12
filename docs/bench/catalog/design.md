# Catalog design

What the catalog measures, and why the experiment is designed as it is. `index.md` says what the catalog is,
`running.md` how to run it, and `results.md` what the reference run found.

## What the catalog measures

### Families and phases

A family is a group of operations on the same data. One run measures one arm of one family, in one cell of the design
and one repetition, in a process of its own. Each operation of the family is timed as a phase, and the phases run in
the order of the table below.

| Family | Phase | What the phase times |
|---|---|---|
| `lookup` | `build` | The rows, a variable family x on them, the (k1, period) pairs that the rows have, and a variable family y on those pairs. |
| | `index` | What the lookups below go through. For dreamor, the phase forces the row indexes of x and y, which the first lookup would build otherwise. For the `map` arm, which has no index of its own, the phase is empty. |
| | `neighbor` | For every row, x at the period before the row's period, if that row exists: `contains()`, then the lookup. |
| | `project` | For every row, y at the row's (k1, period) pair. y is a family on fewer columns than x. |
| | `hit` | x at keys drawn at random from the rows. |
| | `miss` | `contains()` of the same keys, each with its period replaced by a period that no row has. At k = 1, each key is a k1 that is in no row. |
| | `half` | `contains()`, then x if the key is there, over keys of which half are rows. |
| `group` | `build` | The rows and x on them. |
| | `prep_<shape>` | The grouping of the rows by the columns that the shape keeps. |
| | `sum_<shape>` | One expression for each group: x summed over the group's rows. |
| | `sum_where` | One expression for each group of the inner shape: x summed over the group's rows whose period has an even serial. |
| `setop` | `build` | The rows. |
| | `select` | The rows projected on every column but k1, each projection once. |
| | `unite` | A, then the rows of B that A does not have. |
| | `intersect` | The rows of A that B has. |
| | `difference` | The rows of A that B does not have. |
| | `member` | The rows of the set that B has, found through the membership of B. |
| `slice` | `build` | The rows and x on them. |
| | `prep_<shape>` | The grouping of the rows by the shape's columns, and what a key of the driver reaches its group through. |
| | `slice_<shape>` | One expression for each key of the driver: x summed over the rows of the key's group. |
| | `split` | On the inner shape, two expressions for each key in one pass: one over the rows in even periods, and one over the rows in odd periods. |
| | `coef` | On the inner shape, one expression for each key, with the term c(t) * x for each row. The term is a callable on the row's columns. |
| `make` | `collect` | The rows. |
| | `vars` | One variable for each row, unnamed. |
| | `vars_named` | The same variables, each named `x_<id>_<id>...`. |
| | `join` | The same rows again, as A joined with B on k1. |
| `order` | `build` | The rows and x on them. |
| | `index` | For dreamor, the row index of x and the `Ordered` over the periods. For `identity`, `mixed` and `array`, the arm's hash map or array. For `map`, the phase is empty. The hand-written arms make the positions of the periods before the first phase. |
| | `step` | For every row, the period before the row's period. The phase uses no variable. |
| | `lag` | For every row, x at the period four periods before the row's period, if that row exists. |
| | `window` | For every row, x at each of the four periods up to the row's period, for the rows that exist: a rolling sum. |
| `data` | `load` | N entities, inserted one at a time. Each insertion checks that no entity before it has the same key. |
| | `find_hit` | Each entity, found by its key. |
| | `find_miss` | As many keys that no entity has. |
| | `build` | One variable for each entity. |
| | `solution` | The value of every variable, as a solver hands the values back after a solve. |
| | `value` | The value of each entity. |

The families' own headers, in `bench/catalog/`, describe their phases in more detail: `lookup.hpp`, `group.hpp`,
`setop.hpp`, `slice.hpp`, `make.hpp`, `order.hpp`, and `entities.hpp` for the data family.

- **The shapes of `group` and `slice`.** Each shape is named by the column that its grouping leaves out. The inner
  shape leaves out the period, so each of its groups is a run of the generation order. The lead shape leaves out k1.
  The mid shape leaves out the second column, and only `group` has it, at k >= 3 alone. `group` runs its phases in
  the order `prep_inner`, `sum_inner`, `sum_where`, `prep_lead`, `sum_lead`, `prep_mid`, `sum_mid`. `slice` runs
  `prep_inner`, `slice_inner`, `split`, `coef`, `prep_lead`, `slice_lead`.
- **The drivers of `slice`.** A driver is the set that a model walks to write one expression for each key, such as a
  balance over the items and the periods. The driver of a shape holds every key of the shape. Beside each key, it
  holds one key that no row has, and the two keys of a pair come in a random order. So half of the keys reach an
  empty slice, as some (item, period) pairs have no production.
- **The term of `coef`.** The coefficient c(t) is 1 plus the period's serial modulo 7.
- **A and B of `setop`.** A holds the rows of the set that a predicate on (k1, period) keeps, which are about two
  rows in three. B is generated apart, from the same lists with every period moved one on. A predicate on B's last
  two columns keeps some of its rows, and the predicate of a sparse cell keeps some too. So B overlaps A in part. A
  and B are made after `build`, and neither is measured.
- **A and B of `make`.** A holds every column but the period, and B holds k1 and the period. Both are made before
  the first phase, and neither is measured. The join makes the rows that `collect` makes, so the checks of the two
  phases agree.
- **The keys of the lookups.** `hit` takes up to `--lookups` keys, 1,000,000 by default. The keys are the rows,
  walked once and shuffled, so each key is a row drawn uniformly and each row is drawn at most once, whatever the
  sizes of the lists. Each key of `miss` is the key of `hit` with the period that no row has. Each key of `half` is
  the key of `hit` or the key of `miss`, at random.
- **The total.** The report's `total` sums every phase but `build`. The `make` family has no `build` phase, so its
  total sums all of its phases.

### Arms

The catalog compares dreamor with hand-written arms. Each hand-written arm is written as a hand-coder writes it with the
standard library, for its one operation alone, without dreamor's checks, exception safety or generality
([Benchmarks](../index.md)). Most hand-written arms pack the ids of a row into one integer, with column 0 the most significant.
In each family, one hand-written arm is the usual code, which works on the entities themselves.

| Family | Arm | What the arm does |
|---|---|---|
| `lookup` | `identity` | Packs the key, and finds the row through a `std::unordered_map` with libstdc++'s identity hash on integers. |
| | `mixed` | Packs the key, and finds the row through a `std::unordered_map` with a mixed hash, splitmix64. |
| | `array` | Packs the key, and finds the row through a direct array over every combination of ids. The arm runs only while the array takes at most 16 slots for each row. |
| | `map` | The usual code. It keeps a `std::map` from the row's entities to its variable, filled as the rows are generated, as `examples/production_planning_plain.hpp` does. |
| `group` | `identity`, `mixed` | Number the groups through a `std::unordered_map` from the group's packed key, and count the rows into place (CSR). |
| | `sort` | Sorts (key, row) pairs, and reads the groups off as runs. |
| | `map` | The usual code. It keeps a `std::map` from the group's entities to its rows. |
| `setop` | `identity`, `mixed` | Keep a `std::unordered_set` of packed keys. |
| | `merge` | Sorts the packed keys, and uses `<algorithm>`: `std::sort`, `std::unique`, `std::set_union`, `std::set_intersection`, `std::set_difference` and `std::binary_search`. |
| | `set` | The usual code. It keeps a `std::set` of the rows' entities. |
| `slice` | `identity`, `mixed`, `sort`, `map` | Build the groupings of `group`. A key reaches its group through the group's number in the `std::unordered_map`, through `std::lower_bound` over the sorted keys, or through `std::map::find`. |
| `make` | `identity`, `mixed`, `sort`, `map` | Collect the rows with nested loops into a `std::vector`, make a variable for each row, and write the names with `std::to_chars`. That code is the same in every hand-written arm. The join differs: `identity` and `mixed` group B's periods by k1 through a `std::unordered_map`, `sort` sorts B and searches it with `std::equal_range`, and `map` keeps a `std::map` from k1 to its periods. |
| `order` | `identity`, `mixed`, `array`, `map` | Find the variables as the arms of `lookup` do. Each arm keeps the position of each period beside the entity, and does arithmetic on the position where dreamor uses `Ordered`. |
| `data` | `umap` | Keeps a `std::vector` of the entities and a `std::unordered_map` from the key to the entity's place. The variables are in an `IloNumVarArray` and the values in an `IloNumArray`, both by that place. |
| | `map` | The usual code. It keeps `std::map`s by the key. |

In `group` and `slice`, the arms `identity`, `mixed` and `sort` take the groups of the inner shape as runs, as a
hand-coder who knows the generation order would. dreamor's `groupBy` finds the runs by itself.

The dreamor arm builds the set of every family in set-builder notation. `bench/catalog/dreamor/arm.hpp` writes it
as follows, where `ListOf<I>` returns the list of column I that a k1 offers:

```cpp
of<cK<J>...>(in<cK<0>>(d.k1s), in<cK<I + 1>>(ListOf<I + 1>{&d})...)
```

If the cell is sparse, the arm adds `.where(Keep{&d})`, which keeps the rows that the sparse predicate keeps.

Each cell compares dreamor with every hand-written arm that runs in the cell, and with the fastest of them, `best`. A
hand-coder who knows the data picks one arm, and which arm wins is part of the result.

If an arm cannot run a cell, the arm is unsupported there. The report counts that as a result, under Coverage, not as
a gap to fill. Two cases occur:

- The `array` arm is unsupported where the product of the id counts exceeds 16 slots for each row.
- Every arm that packs keys is unsupported where the ids of the key it packs do not fit in 64 bits.

Filling those cells with wider integers or with a hash written for the purpose would design the hand-coder's code for
the hand-coder. The comparison would then no longer be with what a hand-coder writes. The usual code runs in every
cell, so `best` is the usual code where packing does not apply.

### Factors

| Factor | Levels | What the higher level exercises |
|---|---|---|
| k | 1 / 2 / 3 / 5 / 8 | More columns. Column 0 is k1. The k - 2 middle columns come next, each from a list that the k1 offers. The period is the last column. |
| N | 1e5 / 1e6 | More rows. |
| O | shared / owned | Each entity of a middle column belongs to one k1. |
| D | dense / sparse | A predicate keeps one candidate row in ten, so the cell has ten times as many k1s. |
| S | uniform / skewed | The list sizes follow a Pareto law (alpha 1.5) around the same mean, capped at 50 times it, and the periods' at 4 times. The caps make the lists of periods about 19% shorter on average, and the middle ones about 5%. |
| G | large / small | The lists are short. Large lists hold 100 periods and 50 middle combinations, and small lists hold 10 and 5. |
| L | ordered / shuffled | The lists are in a random order, so the rows come in that order. |
| I | aligned / permuted | The ids of every column are in a random order. |

L and I split one factor, the order of the data, in two. L changes the order in which the rows come, which every
arm walks. I changes only the integers that the hand-written arms pack.

### The number of columns, k

Models index their variables by one column, as in `y[l]`, up to about seven columns. For example, an energy system
model indexes its flow by region, vintage, period, process, commodity and time slice, and a scenario adds one more
column. Past that, there is little to measure. The levels take the common counts 1, 2 and 3. They also take 5 and 8, the top. Five is the first
count past the six arguments that x86-64 passes in registers: a lookup passes `this`, the rows and the k columns.

- **k = 1.** A row is its k1 alone, with no period. So only `lookup` and `make` run at k = 1. `lookup` has no y, no
  `neighbor` and no `project` there, and `make` has no `join`. The factors O, S, G and L have nothing to act on, so
  they change no data, and their cells repeat each other. A `total` at k = 1 sums the phases that k = 1 has.
- **The misses at k = 1.** Each miss looks up a k1 that is in no row, a different k1 for each miss. As many such k1s as
  there are rows follow the k1s of the rows. At k > 1 a miss is a hit's row with an absent period, so the misses spread over the k1s of the rows. A single absent key would
  stay in cache from the second miss on.
- **k = 8.** The middle columns make the same combinations as at k = 5: 64 on large lists and 8 on small lists. On large
  lists each middle column has 2 entities. On small lists the first three middle columns have 2 and the rest have 1.
  So k = 5 and k = 8 differ in their columns alone.
- **The builds for k = 8.** The hand-written arms and the samples hold a row in an array as wide as the widest k of the
  build, with the columns past k null. So `build/catalog` and `build/catalog_hand` are 5 wide and run k <= 5.
  `build/catalog8` and `build/catalog_hand8` are 8 wide and run k = 8. `build.sh` builds all four.

### The data family

The data family has entities, not rows, so of the factors above only N applies. Its own two-level factors run in
full, 8 cells with the two levels of N:

- `key` is an integer id or a 13-character code such as `ITEM000012345`.
- `access` is the order of the lookups: the order in which the entities were loaded, or a random order.

## Why the experiment is designed this way

- **Screening first.** The six two-level factors run as a 2^(6-2) fraction of resolution IV, 16 cells. The
  generators are L = ODS and I = DSG, and the defining relation is I = ODSL = DSGI = OGLI. Every main effect is clear
  of every two-factor interaction, and the two-factor interactions are aliased with each other. A fraction of that kind
  ranks the factors for a fraction of the price, as screening designs do (Jain, *The Art of Computer Systems
  Performance Analysis*, 1991, ch. 19; NIST/SEMATECH e-Handbook of Statistical Methods, 5.3.3).
- **Ratios.** Times enter on a log scale, so effects are ratios.
- **Resolution VI.** `--resolution 6` runs the half fraction I = ODSGLI, 32 cells, in which the two-factor
  interactions are clear too. The 32 cells do not extend the 16 cells: quarters added to a resolution IV fraction keep
  a word of length 4. Half of the 16 cells are among the 32.
- **k and N in full.** Both have more than two levels or change the memory regime, so they are crossed with the
  fraction in full, not aliased.
- **One process for each run.** Each run makes its data before its first phase, from the cell and the seed, in the
  same way in every arm. Each arm keeps the data in containers of its own. The dreamor arm keeps the entities of each
  column in an `EntityVector` and refers to them by `Ref`. The hand-written arms keep them in a `std::vector` and refer
  to them by pointer. So the heap that an arm's phases start from differs between the arms only by those containers.
  The process exits without freeing the data.
- **Three kinds of conditions.** The factors above are varied on purpose and reported as effects. The memory layout
  that a run lands in is the operating environment: where the stack starts, which holes the heap has, and where a hot
  loop falls against a cache line. The arm does not choose that layout, and a real program does not fix it. Timer
  resolution, frequency changes and other processes are measurement noise, which is kept down and recorded.
- **The layout is sampled, not fixed.** How an arm responds to the layout is a property of the arm, and an arm that is
  less sensitive is the better design. Which layout one run happens to draw is not a property of the arm. Every arm is
  its own binary in its own process, so it draws its own layout. Under one fixed setup, a difference between arms is
  therefore confounded with the layouts that they drew (Mytkowicz et al., ASPLOS 2009, doi:10.1145/1508244.1508275;
  Curtsinger and Berger, ASPLOS 2013, doi:10.1145/2451116.2451141). So each repetition runs under a layout of its own,
  and every arm of a repetition gets the same layout:
  - An environment padding of its own length, from 0 to 4095 bytes, moves the stack.
  - A heap seed of its own preconditions the heap before anything is built. Blocks of random sizes are allocated, and
    a random half of them are freed, because a fresh heap is an allocator's best case (van Kempen and Berger,
    ISMM '26, arXiv:2605.17119).
  - A code layout of its own comes from a copy of every binary that `LAYOUTS=n ./build.sh` links for the repetition.
    ASLR moves code by whole pages, so the build alone decides where a function falls in its page and which
    functions are next to it. Each copy puts every function in a section of its own, and lld shuffles the sections by
    the repetition's seed, with a padding of the repetition's own size among them (`bench/common/layout.py`).
- **The seeds.** `run.py` draws each repetition's padding and heap seed from `--seed`. `build.sh` draws each
  repetition's code layout from `LAYOUT_SEED`, and `run.py` refuses copies built with another seed than `--seed`. So
  the same seed, compiler and source build the same binaries again. `meta.json` records each copy's seeds and sha256.
  ASLR stays on.
- **The ratio is an expectation over layouts.** The report gives the variation of each arm between layouts beside the
  ratios, as a property of the arm.
- **How fragmented the heap starts is a choice.** Real model code reads its data before it builds, so the heap is not
  fresh. How far from fresh it is, is not known. Half of 65,536 blocks of 16 to 1023 bytes is an assumption, not a
  measurement.
- **Repetitions.** Screening runs 2 repetitions. A run that a conclusion rests on runs 5, the fewest from which a
  variance can be estimated (Kalibera and Jones, ISMM 2013, doi:10.1145/2464157.2464160).
- **Ratios with their base.** The report gives its ratio tables with a 95% interval, and gives the absolute times
  beside the ratios (Hoefler and Belli, SC15, doi:10.1145/2807591.2807644).
- **Checks.** Each phase that hands out variables, rows or values folds them into one number, and every arm must
  agree on that number. A phase that only builds a structure, such as `build`, `index`, `load` or a `prep_*`
  phase, has no check. A phase
  that makes variables folds their upper bounds, which are tags of their rows' serials. A phase that makes rows folds
  hashes of the rows' serials.

### Why the catalog measures against a stub

What a real solver spends to create a variable or a row depends on the solver's insides and on the CPU, not on the
containers in front of it, and it falls on both arms alike. A hand-written / dreamor ratio measured through a real
solver therefore mixes the containers with the solver. For this reason, the catalog measures against a stub. How much
of a real build the containers are is what the scenarios answer, as they report the stub beside every solver.
