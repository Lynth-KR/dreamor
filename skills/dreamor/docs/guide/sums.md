<!-- Copied from docs/guide/sums.md (blob 7dec38fc6778), tests/docs/highs/sums.cpp (blob bfe79aee77ea), tests/docs/highs/plan.hpp (blob 001fb40119a6) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Sums

A sum adds up a term over rows. The hours that M2 works in period 1, for example, add up one term for each item that
M2 makes: the hours that a unit of the item takes, times the units made. `sumExpr(rows, term)` writes such a sum.
`rows` is a set, or a part of one, and `term` returns the term of one row. `build()` then makes the solver's
expression, which on HiGHS is a `HighsLinearExpression`.

The examples use the [guide's model](index.md) on HiGHS:

- `s` holds the model's sets. `s.PRODUCE` holds each machine with each needed item that the machine makes, in each
  period: M1 with the frame and the plate, and M2 with the plate, the bolt and the nut, each in the three periods.
  [Match rows with a set](relational-algebra.md#match-rows-with-a-set) builds it.
- `vMake` and `vStock` are the plan's variable families. `vMake(m, i, t)` is how much machine `m` makes of item `i` in
  period `t`, and `vStock(i, t)` is how much of item `i` is kept at the end of period `t`.
  [Make the model's families](variable-families.md#make-the-models-families) makes them.
- `m1` and `m2` are `Ref`s to the machines M1 and M2, and `frame`, `plate`, `bolt` and `nut` are `Ref`s to the items.
  [Entities](entities.md#look-an-entity-up-by-its-key) looks them up.

## Add a term up over a set

Suppose you want the hours that the machines work over the whole plan. For each row `(m, i, t)` of `s.PRODUCE`, the
term is the hours that a unit of `i` takes times `vMake(m, i, t)`, and the sum adds the terms of every row:
`Σ_{(m, i, t) ∈ produce} time(i) * make(m, i, t)`. Write the term as a callable, and pass it with the set to
`sumExpr`:

```cpp
const auto hours = [&vMake](cMachine m, cItem i, cPeriod t) { return i->time * vMake(m, i, t); };

const HighsLinearExpression totalHours = sumExpr(s.PRODUCE, hours).build();  // the hours of every run
```

- `hours` is a column callable: a lambda whose parameters are columns. It returns the term of one row, the hours that a
  unit of the item takes times the variable of the units made.
- `sumExpr(s.PRODUCE, hours)` returns a sum that is not built yet.
- `build()` calls `hours` on each row of `s.PRODUCE`, and adds the terms up into a `HighsLinearExpression`.

`totalHours` has a term for each of the 15 rows of `s.PRODUCE`. The coefficient of each term is the hours that a unit
of the row's item takes: 1 on M1's frames in period 1, and 0.1 on M2's nuts in period 3. Until `build()`, the sum is
not an expression, so that `where` and `whereEach`, in a section below, can still narrow it.

For what `sumExpr` takes and what `build()` returns, see [`sumExpr`](../reference/dreamor-highs.md#sumexpr) and
[`build`](../reference/sums.md#unbuiltsum-build). On Concert, `sumExpr` takes the environment as its first argument,
and on MathOpt, `build()` returns a MathOpt expression; see [Concert's `sumExpr`](../reference/dreamor-concert.md#sumexpr)
and [MathOpt's `sumExpr`](../reference/dreamor-mathopt.md#sumexpr).

## Add a term up over a group's rows

Each machine has a limit on its hours in each period, so the model needs the hours of each machine in each period
apart: a sum over one machine's rows in one period. Group the rows of `s.PRODUCE` by machine and period, and add the
term up over one group:

```cpp
const auto byMachinePeriod = s.PRODUCE.groupBy<cMachine, cPeriod>();

const HighsLinearExpression m2Hours = sumExpr(byMachinePeriod.slice(m2, 1), hours).build();  // M2 in period 1
```

- `groupBy<cMachine, cPeriod>()` groups the rows of `s.PRODUCE` by their machine and their period.
- `slice(m2, 1)` returns the rows of the group whose machine is M2 and whose period is 1.
- `sumExpr` adds `hours`, from the section above, up over those rows.

M2 makes the plate, the bolt and the nut, so the group holds three rows, and `m2Hours` has three terms:
`0.5 * make(M2, plate, 1) + 0.1 * make(M2, bolt, 1) + 0.1 * make(M2, nut, 1)`. The plan writes this sum for each
machine in each period, as a row of the model with the machine's hours as its upper bound. The plan's row for M2 in
period 1 holds the three terms of `m2Hours`, up to M2's 3 hours.

For how to group a set, see [Grouping](grouping.md) and [`slice`](../reference/grouped.md#grouped-slice). For the loop
that writes a row for each group, see
[Add a row for each group of rows](constraints-and-objective.md#add-a-row-for-each-group-of-rows).

## Add a variable family up as the term

Often the term of a row is a variable and nothing more. Suppose you want the stock of every item that M2 makes, in
every period. The rows are M2's group of `s.PRODUCE`, and the term of each row is `vStock` of the row's item and
period. Pass the family itself as the term:

```cpp
const auto byMachine = s.PRODUCE.groupBy<cMachine>();

const HighsLinearExpression m2Stock = sumExpr(byMachine.slice(m2), vStock).build();  // what M2 makes, kept
```

- `groupBy<cMachine>()` groups the rows of `s.PRODUCE` by their machine, and `slice(m2)` returns M2's rows.
- `vStock` is the term. A `VarTable` is a column callable over its key columns, so the sum calls it on each row. It
  reads the row's item and period, and leaves out the machine, which is not one of its keys.

M2's rows are its plate, bolt and nut in the three periods, 9 rows. `m2Stock` adds the stock of each of the three
items at the end of each period: one term for each row, each with the coefficient 1.

For a `VarTable` as a term, see [Sums](../reference/sums.md) and
[`operator()`](../reference/vartable.md#vartablecore-operator-call).

## Add up what the parents take

An item's balance in a period needs two sums. The first adds what every machine makes of the item. The second adds
what the items made from it take: a frame made takes 2 plates, so the plates that the frames take are 2 times the
frames made. `s.CONSUME` pairs each line of the bill of materials, a parent and a child, with each row of `s.PRODUCE`
that makes the parent: a machine and a period. `quantity` holds each line's quantity, by its parent and child.
[Make a table from records](data-on-a-set.md#make-a-table-from-records) makes it. The plan writes the balance in
`addBalance`:

```cpp
// Stock carried in + made - what customers take - what the parents made take = stock carried out
void addBalance(const Sets& s, const BomTable& quantity) {
  const auto madeByItemPeriod = s.PRODUCE.groupBy<cItem, cPeriod>();
  const auto takenByChild     = s.CONSUME.groupBy<cChild, cPeriod>();
  const auto taken = [&](cParent a, cChild b, cMachine m, cPeriod t) { return quantity(a, b) * vMake(m, a, t); };
  const auto orderedPeriods = s.PERIODS.ordered();
  for (const auto& [i, t] : s.STOCK) {
    HighsLinearExpression inflow = sumExpr(madeByItemPeriod.slice(i, t), vMake).build();
    if (const auto* before = orderedPeriods.prev(t)) { inflow += vStock(i, *before); }
    const auto used = sumExpr(takenByChild.slice(i, t), taken).build();
    addConstr(env, inflow - used - vStock(i, t) == i->demand[t - 1], dreamor::indexedName("Balance", i, t));
  }
}

```

- `madeByItemPeriod` groups the rows of `s.PRODUCE` by item and period, and
  `sumExpr(madeByItemPeriod.slice(i, t), vMake)` adds up what every machine makes of item `i` in period `t`.
- `takenByChild` groups the rows of `s.CONSUME` by their child and period.
- `taken` returns the term of one row of `s.CONSUME`: the line's quantity, `quantity(a, b)`, times what machine `m`
  makes of the parent `a` in period `t`.
- `sumExpr(takenByChild.slice(i, t), taken)` adds up what every parent made in period `t` takes of item `i`.

The plate's balance in period 1 holds what M1 and M2 make of the plate, less 2 times the frames that M1 makes, less the
plate's stock at the end of the period. The bracket takes a plate too, but the plan does not need the bracket, so
`s.CONSUME` holds no line of it. The row is
`make(M1, plate, 1) + make(M2, plate, 1) - 2 * make(M1, frame, 1) - stock(plate, 1) = 0`, because customers buy no
plates.

For the rest of the row, the stock carried in from the period before, see
[Read the period before where there is one](constraints-and-objective.md#read-the-period-before-where-there-is-one).

## Add up part of the rows, or split them among sums

Suppose you want the hours of period 1 alone. Suppose you also want the hours of the items that customers buy apart
from the hours of the other items. To keep only some rows in a sum, add `where`. To split the rows among several sums,
add `whereEach`:

```cpp
const auto in_period_1 = [](cPeriod t) { return t == 1; };
const auto is_sold     = [](cItem i) { return std::ranges::max(i->demand) > 0; };
const auto is_unsold   = [&is_sold](cItem i) { return !is_sold(i); };

const HighsLinearExpression firstHours = sumExpr(s.PRODUCE, hours).where(in_period_1).build();
const auto [soldHours, unsoldHours]    = sumExpr(s.PRODUCE, hours).whereEach(is_sold, is_unsold).build();
```

- `in_period_1`, `is_sold` and `is_unsold` are column callables that return `true` or `false` for a row.
  `is_sold` is true for an item whose demand is above 0 in some period.
- `where(in_period_1)` keeps the rows for which `in_period_1` is true, and `build()` makes one expression of them.
- `whereEach(is_sold, is_unsold)` makes one sum for each callable. `build()` returns a
  `std::array<HighsLinearExpression, 2>`, with the sums in the order of the callables, and the structured binding
  names the two.

`firstHours` has the 5 terms of period 1: M1's frame and plate, and M2's plate, bolt and nut. Customers buy the frame
and the nut. So `soldHours` has the terms of M1's frames and M2's nuts in the three periods, 6 terms, and `unsoldHours`
has the terms of M1's plates and M2's plates and bolts in the three periods, 9 terms.

For what each call takes, see [`where`](../reference/sums.md#unbuiltsum-where) and
[`whereEach`](../reference/sums.md#unbuiltsum-whereeach).

## Add a term up over a plain container

A sum can run over a container that is not a set. Suppose you want the stock of M2's items that is left after the last
period. A machine's items are in its `items`, a `std::vector` of `Ref`s. `p` is the plant, and `p.periods` is the
number of periods, 3:

```cpp
const int last         = p.periods;
const auto kept_at_end = [&vStock, last](cItem i) { return vStock(i, last); };

const HighsLinearExpression m2Left = sumExpr<cItem>(m2->items, kept_at_end).build();  // M2's items, left over
```

- `kept_at_end` returns the stock variable of an item at the end of the last period.
- `sumExpr<cItem>(m2->items, kept_at_end)` adds `kept_at_end` up over M2's items. The template argument names the
  column that the container's elements fill: each `Ref` fills `cItem`, which `kept_at_end` reads.

M2 makes the plate, the bolt and the nut, so `m2Left` is `stock(plate, 3) + stock(bolt, 3) + stock(nut, 3)`.

For what a container must be, see the introduction of [Sums](../reference/sums.md) and
[`sumExpr`](../reference/dreamor-highs.md#sumexpr).

## See also

- [Sums](../reference/sums.md): the sum before `build()`, `where`, `whereEach` and `build`.
- [Variable families](variable-families.md): the variables that a sum adds up.
- [Values over a set](values-over-a-set.md): sums of numbers, not of variables.
- [Constraints and objective](constraints-and-objective.md): the rows and the cost made from sums.
- [Solutions](solutions.md): the value of an expression after a solve.
