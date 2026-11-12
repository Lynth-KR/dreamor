# Deriving sets

The rows of a set cannot be changed. To get other rows from a set that you have, derive a new set from it: keep some
of its rows or some of its columns, make each of its rows anew, or sort its rows. Each derivation reads one set and
returns a new one, and the set that it reads stays as it was. To combine two sets into one, see
[Set algebra](set-algebra.md).

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket.
- `machines` holds the machines M1 and M2. M1 makes the frame, the plate and the bracket. M2 makes the plate, the
  bolt and the nut.
- `periods` is the number of periods, 3.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. A period is an `int`.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns.

Most sections derive their sets from `sMAKE`, which holds every machine with each item that the machine makes, in
each period. A chain builds it, as [Relational algebra](relational-algebra.md) shows:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:make"
```

`sPERIODS` holds the periods 1, 2 and 3. `sMAKE` holds M1 with the frame, the plate and the bracket, and M2 with the
plate, the bolt and the nut, each in the three periods: 18 rows. The rows come in the order in which the chain made
them: M1's frame in periods 1, 2 and 3, then M1's plate in each period, and so on to M2's nut in period 3.

## Keep the rows that meet conditions

Suppose you want the rows of `sMAKE` where a batch of 8 units of the item fits in the machine's hours for one period,
from period 2 on. To keep the rows of a set that meet conditions, call `where` with the conditions:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:where"
```

- `fits_batch` is true if 8 units of the item take no more than the machine's hours. `after_first` is true for every
  period after period 1.
- `sMAKE.where(fits_batch, after_first)` returns a new set with the rows of `sMAKE` for which both conditions are
  true, in the order of `sMAKE`.

`fits_batch` and `after_first` are column callables: lambdas whose parameters are columns. `where` calls each one with
those columns of a row, so each condition reads only the columns that it needs.
[Write a callable over columns](columns.md#write-a-callable-over-columns) shows how to write one.

M1 has 4 hours in a period, and M2 has 3. A batch of 8 frames takes 8 hours, so the frame does not fit on M1. A batch
of 8 plates takes 4 hours, so the plate does not fit on M2. `sBATCH` holds M1 with the plate and the bracket, and M2
with the bolt and the nut, each in periods 2 and 3: 8 rows. `sMAKE` still holds its 18 rows.

For what `where` takes, see [`where`](../reference/indexset.md#indexset-where).

## Keep some columns

Some of a model's rows do not depend on the period, such as the rows that say which machine makes which item.
Suppose you want each pair of a machine and an item that the machine makes, and each item that some machine makes,
each once. To keep some of a set's columns, call `select` with those columns:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:select-columns"
```

- `sMAKE.select<cMachine, cItem>()` returns a set of the columns `cMachine` and `cItem`. For each row of `sMAKE`, it
  makes the row of the machine and the item. A row that repeats an earlier one is dropped, so the new set holds each
  row once, where it first appears.
- `sMAKE.select<cItem>()` returns a set of the column `cItem` alone, made in the same way.

`sMAKE` holds each pair of a machine and an item in three periods, so the pair comes three times and `sROUTES` keeps
it once. `sROUTES` holds M1 with the frame, the plate and the bracket, and M2 with the plate, the bolt and the nut: 6
rows. `sMADE` holds the frame, the plate, the bracket, the bolt and the nut. The plate is made on M1 and on M2, and
`sMADE` holds it once, where M1's plate first appears.

For what `select` takes, see [`select`](../reference/indexset.md#indexset-select-columns).

## Make each row anew

Sometimes the rows that you need are made from the rows of another set: the same item in another column, or the same
item in another period. Suppose you want the items made as parents of the bill of materials, to match them with the
bill's lines on the column `cParent`. Suppose you also want each item made in a period, moved to the period after it,
as for the stock that a period passes on. To make each row anew, call `select` with a callable that returns the new
row:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:select-expr"
```

- `as_parent` returns the item as a `cParent`. `cParent` holds a `Ref<Item>`, as `cItem` does, so the item moves to
  the other column.
- `next_period` returns a `std::tuple` of the item and the period after `t`.
- `sMAKE.select(as_parent)` returns a set of the rows that `as_parent` returns for the rows of `sMAKE`. A row that
  repeats an earlier one is dropped, as with `select` of columns.
- `sMAKE.select(next_period)` returns a set of the rows that `next_period` returns.

`sMADE_PARENTS` is an `IndexSet<cParent>` of the five items that `sMADE` holds. As parents, the items can match the
bill of materials' lines in a join, which [Match rows with a set](relational-algebra.md#match-rows-with-a-set) shows.
`sNEXT` is an `IndexSet<cItem, cPeriod>`. M1's plate and M2's plate in one period make the same row, which `sNEXT`
holds once. So `sNEXT` holds the five items in periods 2, 3 and 4: 15 rows. Period 4 is not a period of the plan:
`select` keeps every row that the callable returns.

For what `select` takes, see [`select`](../reference/indexset.md#indexset-select-expression).

## Sort the rows

Suppose you want the items made, from the one that takes the least time on a machine to the one that takes the most,
and the other way round. Suppose you also want each pair of a machine and an item sorted by the item, and by the
machine for an item that two machines make. To sort a set's rows, call `orderBy` with a callable that returns the key
to sort by:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:order-by"
```

- `time_of` returns the hours that a unit of the item takes. `item_then_maker` returns a `std::tuple` of the item and
  the machine, which `orderBy` compares item first.
- `sMADE.orderBy(time_of)` returns a new set with the rows of `sMADE`, the smallest time first.
- `sMADE.orderBy(time_of, dreamor::descending)` returns them with the largest time first.
- `sROUTES.orderBy(item_then_maker)` returns the pairs of `sROUTES`, sorted by item and then by machine.

Rows whose keys are equal keep the order that they have in the set. A `Ref` compares by its entity's primary key,
which is the name for an item and for a machine.

A unit of the bolt or the nut takes 0.1 hours, a unit of the plate or the bracket 0.5 hours, and a unit of the frame
1 hour. `sQUICK_FIRST` holds the bolt, the nut, the plate, the bracket and the frame, in that order. The bolt comes
before the nut, and the plate before the bracket, because `sMADE` holds them in that order. `sSLOW_FIRST` holds the
frame, the plate, the bracket, the bolt and the nut, with the equal times in the order of `sMADE` too. `sBY_ITEM` holds
the pairs in the order of the items' names: M2's bolt, M1's bracket, M1's frame, M2's nut, M1's plate and M2's plate.
M1's plate comes before M2's, because "M1" comes before "M2".

For what `orderBy` takes, see [`orderBy`](../reference/indexset.md#indexset-orderby) and
[`descending`](../reference/indexset.md#descending). To read positions and neighbours in the new order, see
[Ordered sets](ordered-sets.md).

## Ask whether a row is in a set

A model often needs to know whether one combination is in a set, such as whether a machine makes an item. To ask,
call `contains` with the row's key:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:contains"
```

`contains` takes the key's columns in the order of the set, here a machine and an item, and returns true if the set
has the row. `machines.at("M2")` and `items.at("bolt")` return a `Ref` to M2 and a `Ref` to the bolt.

`sROUTES`, from [Keep some columns](#keep-some-columns), holds each machine with each item that the machine makes.
M2 makes the bolt, so `m2_makes_bolts` is true. M1 does not make the bolt, so `m1_makes_bolts` is false.

For the keys that `contains` takes, see [`contains`](../reference/indexset.md#indexset-contains) and
[Keys](../reference/keys.md).

## Keep the rows that another set has, or lacks

Suppose you want the items made that no other item takes as a part: the products at the top of the bill of
materials. Suppose you also want the rows of `sNEXT` whose period is a period of the plan. Each keeps the rows of one
set by the rows of another. To use a set as a condition, call its `membership()`, and pass the condition to `where`:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:membership"
```

`quantity` is the bill of materials, a `ParamTable<double, cParent, cChild>`. Its `keys()` are its lines, each a
parent and a child. [Make a table from records](data-on-a-set.md#make-a-table-from-records) makes it.

- `as_item` returns the child as a `cItem`. `quantity.keys().select(as_item)` returns `sPARTS`, the child of each
  line, each item once.
- `sPARTS.membership()` returns a condition that is true for the rows that `sPARTS` holds. `!` returns the opposite
  condition, which is true for the rows that `sPARTS` does not hold. `sMADE.where` keeps the items for which the
  opposite condition is true.
- `sPERIODS.membership()` returns a condition that reads the column `cPeriod` alone, because `sPERIODS` has no other
  column. `sNEXT.where` keeps the rows whose period `sPERIODS` holds, whatever their item.

The lines take the plate, the bolt and the nut, so `sPARTS` holds these three items. `sPRODUCTS` holds the frame and
the bracket, which no line takes. `sNEXT`, from [Make each row anew](#make-each-row-anew), holds the five items in
periods 2, 3 and 4, and `sPERIODS` holds the periods 1, 2 and 3. `sNEXT_IN_PLAN` holds the five items in periods 2
and 3: 10 rows.

For what a membership condition reads, see [`membership`](../reference/indexset.md#indexset-membership) and
[`Membership`](../reference/membership.md#membership).

## Use a set as a chain's condition

Suppose you want each machine with each item that the machine makes and that another item takes as a part. To keep
the rows of a chain whose columns are in a set, pass the set's membership to the chain's `where`:

```cpp
--8<-- "tests/docs/guide/deriving_sets.cpp:membership-chain"
```

`from<cMachine>(machines).lateral<cItem>(items_of)` makes a row for each machine and each item that the machine
makes, and `where(sPARTS.membership())` keeps the rows whose item `sPARTS` holds.

`sPARTS`, from the section above, holds the plate, the bolt and the nut. `sPART_ROUTES` holds M1 with the plate, and
M2 with the plate, the bolt and the nut. M1's frame and bracket are not parts of another item, so the chain drops
them.

For what a chain's `where` takes, see [`where`](../reference/relational-algebra.md#chain-where).

## See also

- [IndexSet](../reference/indexset.md): every member of a set.
- [Membership](../reference/membership.md): the condition that `membership()` returns.
- [Relational algebra](relational-algebra.md): a set built step by step by a chain.
- [Set algebra](set-algebra.md): two sets combined into one, or compared.
- [Grouping](grouping.md): a set's rows divided into groups.
- [Ordered sets](ordered-sets.md): positions and neighbours in a set's order.
