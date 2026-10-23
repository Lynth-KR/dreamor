# Visit order

`makeSet` visits its sources in an order `plan.hpp` picks at compile time. This folder measures
how good that order is, two ways: a cost model over made-up sets of 3 to 16 sources, which
measures the planner's logic independently of any machine, and a timed experiment on three
sources, which measures what orders cost on a real CPU. Neither is part of the container
benchmark.

## The planner

Greedy, from column names alone - which columns each source binds and reads, and which each
predicate reads; no sizes, no selectivities:

1. A source is eligible once every column it depends on is bound.
2. Prefer the one that lets the most predicates run next.
3. Then the one fewest columns away from some predicate.
4. Then a dependent source over a container.
5. Then declaration order.

Each predicate runs at the first point its columns are all bound, whatever the order.

## Why these rules

- **Rules 2 and 3** are the "most constrained first" order of subgraph matching. RI (Bonnici et
  al., BMC Bioinformatics 2013) orders a pattern's vertices from the pattern alone: next, the
  vertex with the most neighbours already ordered, then the one closest to them. Sun and Luo's
  comparison of subgraph matchers (SIGMOD 2020) found RI's and GraphQL's orderings the most
  effective.
- **Rule 4** is what statistics-free join ordering does with key/foreign-key joins. Simpli-Squared
  (Datta et al., arXiv 2111.00163) puts one-to-many joins before many-to-many ones because their
  result size is predictable, and on the Join Order Benchmark runs at most 16% slower than
  statistics-based optimizers. A dependent source - a machine's items - is a one-to-many lookup:
  the list it hands back per binding is usually far shorter than a whole container.
- **No statistics** because there are none at compile time, and estimates are where cost-based
  optimizers go wrong: Leis et al. ("How Good Are Query Optimizers, Really?", VLDB 2015) find
  cardinality estimates the main factor in a join order, and the estimators in use routinely
  wrong by orders of magnitude.

## The model

`planVisitOrder` is a `constexpr` function of column masks, so `model.cpp` calls it at run time on
sets it makes up, as `makeSet` does at compile time. Nothing is generated: an order's cost is the
work its nested loops do, in expectation.

- **Cost.** A source visited after the sources in S runs once per partial tuple that survived S.
  With T(S) the product of the sizes in S and the selectivities of the predicates S binds, an
  order costs the sum over its steps of T(sources before) x size(next source) - the candidates
  the loops look at. T depends on the set, not the order, so it is worked out once per subset;
  the best and worst orders come from a dynamic program over subsets, exact at 16 sources.
- **Sets**, drawn at random for each number of sources and each shape:
  - shapes, by which pairs of columns a predicate reads: chain, star, cycle, clique, random, and
    filters (mostly one-column predicates). The columns are shuffled, so that no shape lines up
    with declaration order.
  - with or without dependent sources, each reading one column declared before it.
  - sizes log-uniform: a container 10 - 1000 rows, a dependent source's list per binding 2 - 50
    (`--long-lists`: 10 - 1000, as a container). A predicate keeps 1/100 - 1 of what it sees.
- **Orders set against the planner's:**
  - without rule 4 - declaration order breaking every tie the predicates leave;
  - declared - the order the sources are written in;
  - sizes 1/10 - a greedy that knows the sizes but not the selectivities, taking each predicate
    to keep 1/10;
  - planner + sizes - the planner's rules with sizes breaking the ties they leave, as Kimmig,
    Meyerhenke and Strash (arXiv 1705.09358) break RI's ties by the smaller candidate set;
  - oracle - a greedy that knows sizes and selectivities.

  Regret is an order's cost over the best order's.
- **Checks** (`model --check`): the expected cost against the candidates real nested loops look
  at, on 600 small orders; the dynamic program against listing every order; and the model's own
  statement of the planner's rules, which the variants build on, against `planVisitOrder`.

## The timed experiment

`plan.cpp` generates one set of three columns (A, B, C) in every order it can be visited in and
times each; `run.py` runs every case in every order, repetitions times, in a seeded random order,
one process per run.

A dependent source that hands back the whole container, but reads the column before it, can
only be visited after that column, which forces an order:

```cpp
auto a_after_c = [&](CCol) -> const std::vector<const E*>& { return as; };
auto b_after_a = [&](ACol) -> const std::vector<const E*>& { return bs; };
makeSet<ACol, BCol, CCol>(a_after_c, b_after_a, cs)   // visits C, A, B
```

The rows are what `makeSet<ACol, BCol, CCol>(as, bs, cs)` gives - only the order of the loops
changes. Every variant of a case must produce the same set (rows and an order-free checksum), and
`run.py` stops if one does not.

| shape | predicates (`p` over two columns, `q` over one) | the planner visits |
|---|---|---|
| pair | p(B, C) | B, C, A |
| filters | q(A), q(C) | A, C, B |
| chain | p(A, B), p(B, C) | A, B, C |
| triangle | p(A, B), p(B, C), p(A, C) | A, B, C |
| join | S = A x B where p, U = B x C where p | as written: `makeSet<>(S, U)` or `(U, S)` |

- Sizes: every source at 200 rows, or A or C at 2,000. No source is dependent.
- Selectivity: each predicate keeps 1 in 2 (weak) or 1 in 30 (strong); `filters` makes one
  filter strong and the other weak, both ways round.
- Predicates decide by a hash of the ids (`--preds random`), which a branch predictor cannot
  learn, or keep `(x + y + salt) % den == 0` (`--preds periodic`), the same selectivity in a
  pattern it can.

Per case, `run.py` reports: the planner's order; the best and worst forced order; `regret`, the
planner's order forced over the best forced; `vs decl`, declaration order forced over the
planner's; `late`, one predicate over all columns that runs once they are bound, over the
planner's choice; `chain`, the planner's order forced over the same order chosen freely; and the
predicate calls of the planner's choice and of the best order.

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

`run.py` writes `plan.csv` to `results/<date>_<commit>_plan_<preds>/`.

## Results

The model's: expected work, so independent of the machine. `./model` with its defaults: seed 20261024, 200
sets per (sources, shape, dependent sources or not), sources 3, 4, 5, 6, 8, 10, 12, 14, 16.
Checks: real loops over modelled cost, median 1.000 (p5 0.668, p95 1.492, small sets); dynamic
program against every order listed, no mismatch in 300 sets; the model's rules against
`planVisitOrder`, no mismatch in 300 sets.

Regret over every set:

| Order | Knows | Median | p90 | `--long-lists`: median | p90 |
|---|---|---|---|---|---|
| declared | - | 21.9 | 12,451 | 19.6 | 8,245 |
| without rule 4 | names | 3.61 | 216 | 3.16 | 166 |
| **planner** | names | **3.05** | **149** | 3.19 | 173 |
| sizes 1/10 | sizes | 2.65 | 65 | 2.34 | 47 |
| planner + sizes | names, sizes | 2.18 | 52 | 2.06 | 42 |
| oracle | sizes, selectivities | 1.28 | 18 | 1.16 | 11 |

Median regret at 16 sources, by shape, without / with dependent sources:

| Shape | planner | without rule 4 | declared | planner + sizes | oracle |
|---|---|---|---|---|---|
| chain | 3.38 / 9.01 | 3.38 / 15.7 | 124 / 543 | 2.72 / 18.3 | 1.80 / 6.50 |
| star | 11.7 / 16.5 | 11.7 / 24.8 | 52.5 / 3,063 | 8.81 / 13.9 | 1.15 / 2.89 |
| cycle | 25.4 / 27.8 | 25.4 / 49.0 | 153 / 446 | 21.1 / 45.9 | 1.79 / 8.75 |
| clique | 664 / 16.2 | 664 / 121 | 664 / 121 | 2.02 / 2.29 | 1.34 / 1.70 |
| random | 5.84 / 141 | 5.84 / 204 | 180,134 / 269,537 | 3.01 / 132 | 1.36 / 19.9 |
| filters | 1.01 / 1.02 | 1.01 / 1.05 | 9.39 / 6.15 | 1.00 / 1.00 | 1.00 / 1.03 |

`./model` prints every number of sources, with medians and p90s.
