# Values over a set

A value over a set is computed from the set's rows: whether a condition holds on every row, how many rows it holds
on, or the largest value on the rows. You compute such values from the data, before a solve: a check on the data, a
bound, a choice between two models. Each function takes the rows and a column callable, and calls the callable on
each row with the columns that it reads. You write no loop, and no new set is made.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items. The hours that a unit takes on a machine are 1 for the frame, 0.5 for the plate and the
  bracket, and 0.1 for the bolt and the nut. Customers buy 1, 2 and 2 frames in periods 1, 2 and 3, and 5 nuts in
  period 2, and nothing else.
- `machines` holds the machines. M1 has 4 hours in a period and makes the frame, the plate and the bracket. M2 has 3
  hours and makes the plate, the bolt and the nut.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns. The
sections read two sets of the model:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:sets"
```

`sMAKE` holds each machine with each item that it makes: 3 items on each of the 2 machines, 6 rows. `sDEMAND` holds
each item in each period: 5 items in 3 periods, 15 rows. [Relational algebra](relational-algebra.md) shows how a
chain builds them.

## Ask whether every row holds, some, or none

Before you build a model, check that its data makes sense. Suppose you want to know three things: whether a unit of
each item fits in its machine's hours for one period, whether some machine can make a batch of 8 units of an item in
one period, and whether no period asks for more than 10 units of an item. Ask each question over the rows that it is
about:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:all-any-none"
```

- `dreamor::allOf(sMAKE, fits_a_period)` returns true if `fits_a_period` is true for every row of `sMAKE`.
- `dreamor::anyOf(sMAKE, fits_batch)` returns true if `fits_batch` is true for at least one row.
- `dreamor::noneOf(sDEMAND, is_rush)` returns true if `is_rush` is true for no row of `sDEMAND`.
- `DREAMOR_LOG_ERROR` writes an error line to dreamor's log if `unitsFit` is false.

`unitsFit` is true. The slowest unit on M1 is the frame's, 1 hour of M1's 4, and the slowest on M2 is the plate's,
0.5 hours of M2's 3. So no error line is written. `someBatch` is true too: 8 plates take 4 hours, which fit in M1's 4
hours, though 8 frames, which take 8 hours, do not. `noRush` is true, because the largest demand in a period is 5
nuts.

For each function, see [`allOf`](../reference/values-over-a-set.md#allof),
[`anyOf`](../reference/values-over-a-set.md#anyof), [`noneOf`](../reference/values-over-a-set.md#noneof) and
[`DREAMOR_LOG_ERROR`](../reference/logger.md#dreamor_log_error).

## Count the rows that hold

Suppose you want to know how many pairs of an item and a period need an order: the rows of `sDEMAND` in which
customers buy. To count the rows for which a condition is true, use `countIf`:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:count"
```

`dreamor::countIf(sDEMAND, has_demand)` calls `has_demand` on every row of `sDEMAND` and returns the number of rows
for which it is true.

`sales` is 4: customers buy the frame in each of the three periods, and the nut in period 2.

For what `countIf` takes, see [`countIf`](../reference/values-over-a-set.md#countif).

## Take the largest, the smallest, the sum

A bound or a check often comes from one number of the data: the largest demand in a period, the fewest hours that a
unit takes, or the total demand over the periods. To compute each over a set's rows, use `maxOf`, `minOf` and
`sumValue`:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:max-min-sum"
```

- `dreamor::maxOf(sDEMAND, demand_of)` returns the largest of the values that `demand_of` returns on the rows of
  `sDEMAND`.
- `dreamor::minOf(sMAKE, unit_hours)` returns the smallest of the values that `unit_hours` returns on the rows of
  `sMAKE`. `unit_hours` reads only the item, so `minOf` calls it with the item of each row.
- `dreamor::sumValue(sDEMAND, demand_of)` adds up the values that `demand_of` returns on the rows of `sDEMAND`.

`peak` is 5, the nuts in period 2. `fastest` is 0.1, the hours of a bolt or a nut on M2. `total` is 10, the frames'
1 + 2 + 2 and the nuts' 5.

`sumValue` adds up numbers from the data. To add up a term that a solver reads, such as a variable on each row, use
`sumExpr`, which [Sums](sums.md) shows. For what each function takes, see
[`maxOf`](../reference/values-over-a-set.md#maxof), [`minOf`](../reference/values-over-a-set.md#minof) and
[`sumValue`](../reference/values-over-a-set.md#sumvalue).

## Take a value over a group's rows

Suppose you want the value for one machine alone, such as the slowest unit that M2 makes. Group the rows of `sMAKE`
by machine, and take the value over M2's rows:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:group"
```

- `sMAKE.groupBy<cMachine>()` gathers the rows of `sMAKE` by their machine.
- `byMachine.slice(machines.at("M2"))` returns M2's rows.
- `dreamor::maxOf` takes M2's rows as it takes a set.

M2's rows hold the plate, the bolt and the nut. `slowestOnM2` is 0.5, the plate's hours. The frame takes longer, 1
hour, but M2 does not make it.

For how to group a set and look a group up, see [Grouping](grouping.md),
[`groupBy`](../reference/indexset.md#indexset-groupby) and [`slice`](../reference/grouped.md#grouped-slice).

## Find a row

A value alone does not say which row it comes from. Suppose you want the item and the period of the largest demand,
to write them in a log line. To find a row, ask for a pointer to it:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:find"
```

- `dreamor::maxElement(sDEMAND, demand_of)` returns a pointer to the row on which `demand_of` returns the largest
  value. If several rows have the largest value, the pointer refers to the first of them.
- `dreamor::minElement(sMAKE, unit_hours)` returns a pointer to the row with the smallest value, the first of them
  if there are several.
- `dreamor::findIf(sDEMAND, has_demand)` returns a pointer to the first row for which `has_demand` is true, or
  `nullptr` if there is none.
- `*peakRow` is the row itself, a `std::tuple<cItem, cPeriod>`, and `const auto& [item, period]` takes its columns
  apart.
- `std::format` writes the item as its name, and the period as its number.

`peakRow` points to the nut in period 2. `quickRow` points to M2 with the bolt: the bolt and the nut both take 0.1
hours on M2, and the bolt comes first in `sMAKE`. `firstRow` points to the frame in period 1. `sDEMAND` holds the
items in the order of their names, so the bolt and the bracket come before the frame, and customers buy neither.
`rushRow` is `nullptr`, because no period asks for more than 10 units. `peakText` is `nut in period 2`. Each pointer
that is not `nullptr` points to the row that the set holds, not to a copy of it.

For what each function takes, see [`maxElement`](../reference/values-over-a-set.md#maxelement),
[`minElement`](../reference/values-over-a-set.md#minelement) and [`findIf`](../reference/values-over-a-set.md#findif).

## Take a value over a plain container

Sometimes the rows that you need are not a set: every machine of `machines`, or the items in M1's list. To compute
a value over a container, name the column that each element fills as the template argument:

```cpp
--8<-- "tests/docs/guide/values_over_a_set.cpp:plain-container"
```

- `dreamor::sumValue<cMachine>(machines, capacity_of)` reads each machine of `machines` as a row of one column,
  `cMachine`.
- `dreamor::sumValue<cItem>(machines.at("M1")->items, unit_hours)` reads each `Ref` in M1's list of items as a row
  of one column, `cItem`.

`plantHours` is 7, M1's 4 hours and M2's 3. `m1Units` is 2, the hours of a frame, a plate and a bracket: 1, 0.5 and
0.5.

For the functions that take a plain container, see the introduction of
[Values over a set](../reference/values-over-a-set.md).

## See also

- [Values over a set](../reference/values-over-a-set.md): every function, and which ones take a plain container.
- [Grouping](grouping.md): a group's rows.
- [Data on a set](data-on-a-set.md): a value kept for each row of a set.
- [Sums](sums.md): a sum that a solver reads.
- Debug-build checks: [`maxOf` or `minOf` over no rows](../reference/debug-build-checks.md#aggregate_rows_not_empty).
