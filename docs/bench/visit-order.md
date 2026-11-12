# Visit order

When `build()` makes a set, written as a chain or in set-builder notation, it visits the set's sources in an order
that `plan.hpp` (`include/dreamor/internal/index_set/`) picks at compile time. The study in `bench/plan/` measures
how good that order is, in two ways:

- A cost model over made-up sets of 3 to 16 sources measures the planner's logic, independently of any machine.
- A timed experiment on three sources measures what the orders cost on a real CPU.

Neither is part of the catalog of container benchmarks.

In this study, and in `plan.hpp`, a predicate is a condition that reads columns, such as a column callable given to
`where`. A predicate that reads two columns joins them, and a predicate that reads one column filters it.

## The planner

The planner is greedy. It decides from the column names alone: which columns each source binds and reads, and which
columns each predicate reads. It knows no sizes and no selectivities. At each place in the order, it applies these
rules:

1. A source is eligible once every column that it depends on is bound.
2. Prefer the eligible source that lets the most predicates run next.
3. On a tie, prefer the source after which some predicate misses the fewest columns.
4. On a tie, prefer a dependent source over a container.
5. On a tie, take the source declared first.

Each predicate runs as soon as all of its columns are bound, whatever the order.

## Why these rules

- **Rules 2 and 3** are the "most constrained first" order of subgraph matching. RI (Bonnici et al., BMC
  Bioinformatics 2013) orders a pattern's vertices from the pattern alone. It takes next the vertex with the most
  neighbours already ordered, then the one closest to them. Sun and Luo's comparison of subgraph matchers (SIGMOD
  2020) found the orderings of RI and GraphQL the most effective.
- **Rule 4** is what join ordering without statistics does with key/foreign-key joins. Simpli-Squared (Datta et al.,
  arXiv 2111.00163) puts one-to-many joins before many-to-many joins, because the size of their result is
  predictable. On the Join Order Benchmark, it runs at most 16% slower than optimizers that use statistics. A
  dependent source, such as a machine's items, is a one-to-many lookup. The list that it returns for one binding is
  usually far shorter than a whole container.
- **The planner uses no statistics**, because there are none at compile time, and because estimates are where
  cost-based optimizers go wrong. Leis et al. ("How Good Are Query Optimizers, Really?", VLDB 2015) find that
  cardinality estimates are the main factor in a join order. They also find that the estimators in use are routinely
  wrong by orders of magnitude.

## The cost model

`planVisitOrder` is a `constexpr` function of column masks. So `model.cpp` calls it at run time on sets that it makes
up, as `build()` calls it at compile time. The model generates no rows. An order's cost is the work that its nested
loops do, in expectation.

### Cost

A source visited after the sources in S runs once for each partial row that survived S. Let T(S) be the product of the
sizes in S and of the selectivities of the predicates that S binds. Then an order costs the sum over its steps of
`T(sources before) x size(next source)`. This is the number of candidates that the loops look at. T depends on the set
of sources, not on their order, so the model works it out once for each subset. A dynamic program over the subsets
finds the best and the worst order. It is exact at 16 sources, where listing all 16! orders is not possible.

### Sets

The model draws sets at random, for each number of sources and each shape:

- A shape says which pairs of columns the predicates read: chain, star, cycle, clique, random, or filters. In
  `filters`, the predicates mostly read one column each. The model shuffles the columns, so that no shape lines up
  with declaration order.
- A set has dependent sources or has none. In a set with dependent sources, a source reads one column declared
  before it, with probability 0.4.
- The sizes are log-uniform. A container holds 10 to 1000 rows. A dependent source's list for one binding holds 2 to
  50 elements. With `--long-lists`, the list holds 10 to 1000 elements, as a container does.
- A predicate keeps 1/100 to 1 of the rows that it sees, log-uniform.

### Orders

The model sets these orders against the planner's:

- **without rule 4**: the planner's rules, with declaration order breaking every tie that the predicates leave.
- **declared**: the order in which the sources are written.
- **sizes 1/10**: a greedy order that knows the sizes but not the selectivities, and takes each predicate to keep
  1/10.
- **planner + sizes**: the planner's rules, with the sizes breaking the ties that the rules leave. Kimmig, Meyerhenke
  and Strash (arXiv 1705.09358) break RI's ties in the same way, by the smaller candidate set. The library does not
  implement this order.
- **oracle**: a greedy order that knows the sizes and the selectivities, and visits next the source after which the
  fewest partial rows survive.

An order's regret is its cost over the best order's cost. The planner's rank runs from 0, the best order, to 1, the
worst. Up to 8 sources, the rank is over every order. Above 8, it is over a sample of 4000 orders.

### Checks

`model --check` runs three checks on 300 small sets of 3 to 5 sources:

- The expected cost against the candidates that real nested loops look at, for the planner's order and the oracle's
  order of each set: 600 orders.
- The dynamic program against a list of every order.
- The model's own statement of the planner's rules, on which the variants build, against `planVisitOrder`.

## The timed experiment

`plan.cpp` builds one set of three columns, A, B and C, in one of the orders in which the set can be visited, and
times the build. `run.py` runs every case in every order, `--reps` times, in a seeded random order, one process for
each run. The counting build, `plan_count`, then runs each variant once more and counts the predicate calls.

To force an order, `plan.cpp` writes the sources so that only one order meets their dependencies. The first source
reads its container. Each later source reads a callable that has a parameter of the column before it and returns the
whole container. So the source can be visited only after that column, and the order is the one in which the
generators are written:

```cpp
auto a_after_c = [&](cC) -> const std::vector<dreamor::Ref<E>>& { return as; };
auto b_after_a = [&](cA) -> const std::vector<dreamor::Ref<E>>& { return bs; };
of<cC, cA, cB>(in<cC>(cs), in<cA>(a_after_c), in<cB>(b_after_a))   // visits C, A, B
```

The rows are the rows of `of<cA, cB, cC>(in<cA>(as), in<cB>(bs), in<cC>(cs))`. Only the order of the loops changes.
Every variant of a case must make the same set: `plan.cpp` prints an order-free checksum of the rows and their number,
and `run.py` stops if two variants of a case print different checksums.

The variants of a case are these:

- `free`: the planner's choice.
- `ABC` to `CBA`: each of the six orders, forced.
- `late`: one predicate over all three columns, which runs only once all three are bound, as a filter over the
  finished product does.

The cases come in five shapes. `p` is a predicate over two columns, and `q` a predicate over one:

| Shape | Predicates | The planner visits |
|---|---|---|
| pair | p(B, C) | B, C, A |
| filters | q(A), q(C) | A, C, B |
| chain | p(A, B), p(B, C) | A, B, C |
| triangle | p(A, B), p(B, C), p(A, C) | A, B, C |
| join | S = A x B where p, U = B x C where p | as written: `of<cA, cB, cC>(in<cA, cB>(sS), in<cB, cC>(sU))`, or `sU` first |

A join has no order to choose, because it is visited as its generators are written. So the join shape has two
variants instead: `SU`, the join as written above, and `US`, with `sU` first. Both sets are built before the timer
starts, and only the join is timed.

- **Sizes.** Every source holds 200 rows, or A or C holds 2,000. The join shape runs only the two cases with a big
  source. No source is dependent.
- **Selectivity.** Each predicate keeps 1 in 2 of the rows, which is weak, or 1 in 30, which is strong. In
  `filters`, one filter is strong and the other weak, both ways round.
- **Predicates.** With `--preds random`, the default, a predicate decides by a hash of the ids, which a branch
  predictor cannot learn. With `--preds periodic`, a predicate keeps the rows where `(x + y + salt) % den == 0`: the
  same selectivity, in a pattern that a branch predictor can learn. If an order is faster under random predicates
  only, the order predicts branches better; it does not call the predicates less often.

For each case, `run.py` prints these columns:

- `planned`: the planner's order.
- `free ms`: the median time of the planner's choice.
- `best` and `worst`: the fastest and the slowest forced order, with their times.
- `regret`: the planner's order forced, over the best order forced.
- `vs decl`: declaration order forced, over the planner's order forced.
- `late`: the late filter, over the planner's choice.
- `chain`: the planner's order forced, over the same order chosen freely. It says what forcing an order costs by
  itself.
- `calls free` and `best`: the predicate calls of the planner's choice and of the best order.

For the join shape, `run.py` prints `SU` against `US` instead.

## Running

```sh
bench/plan/build.sh             # ./model; ./plan times, ./plan_count counts predicate calls
cd bench/plan
./model --check                 # the model's checks
./model                         # 3 to 16 sources, 200 sets per row: about a minute
./model --long-lists            # dependent lists as long as containers
python3 run.py                  # the timed experiment, 5 repetitions: a few minutes
python3 run.py --preds periodic
```

`build.sh` builds the three programs with `CXX`, `g++` by default. `./model` takes `--sets N`, `--ks 3,4,...` and
`--seed S` as well. `run.py` takes `--reps`, `--shapes` and `--seed`.

`./model` prints one row for each number of sources, shape, and whether the set has dependent sources. A row gives
the planner's median and 90th-percentile regret, the share of sets in which its regret is at most 1.2, its median
rank, and each other order's median regret. Then it prints, for each order, the median, 90th and 99th percentile of
the regret over all sets.

`run.py` writes `plan.csv` to `bench/results/<date>_<commit>_plan_<preds>/`, or to the path that `--out` names. A rerun
overwrites the file.

## Results

The cost model's results are expected work, so they do not depend on the machine. dreamor 0.9.0's planner gives the
same numbers, to the last digit shown. They come from `./model` with its
defaults: seed 20261024, 200 sets for each number of sources, shape, and presence of dependent sources, and the
numbers of sources 3, 4, 5, 6, 8, 10, 12, 14 and 16.

The checks passed:

- Real loops over modelled cost: median 1.000, 5th percentile 0.668, 95th percentile 1.492, on small sets.
- The dynamic program against every order listed: no mismatch in 300 sets.
- The model's rules against `planVisitOrder`: no mismatch in 300 sets.

Regret over every set:

| Order | Knows | Median | p90 | `--long-lists`: median | p90 |
|---|---|---|---|---|---|
| declared | - | 21.9 | 12,451 | 19.6 | 8,245 |
| without rule 4 | names | 3.61 | 216 | 3.16 | 166 |
| **planner** | names | **3.05** | **149** | 3.19 | 173 |
| sizes 1/10 | sizes | 2.65 | 65 | 2.34 | 47 |
| planner + sizes | names, sizes | 2.18 | 52 | 2.06 | 42 |
| oracle | sizes, selectivities | 1.28 | 18 | 1.16 | 11 |

Median regret at 16 sources, by shape, without and with dependent sources:

| Shape | planner | without rule 4 | declared | planner + sizes | oracle |
|---|---|---|---|---|---|
| chain | 3.38 / 9.01 | 3.38 / 15.7 | 124 / 543 | 2.72 / 18.3 | 1.80 / 6.50 |
| star | 11.7 / 16.5 | 11.7 / 24.8 | 52.5 / 3,063 | 8.81 / 13.9 | 1.15 / 2.89 |
| cycle | 25.4 / 27.8 | 25.4 / 49.0 | 153 / 446 | 21.1 / 45.9 | 1.79 / 8.75 |
| clique | 664 / 16.2 | 664 / 121 | 664 / 121 | 2.02 / 2.29 | 1.34 / 1.70 |
| random | 5.84 / 141 | 5.84 / 204 | 180,134 / 269,537 | 3.01 / 132 | 1.36 / 19.9 |
| filters | 1.01 / 1.02 | 1.01 / 1.05 | 9.39 / 6.15 | 1.00 / 1.00 | 1.00 / 1.03 |
