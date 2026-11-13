# Grouping

A group is the rows of a set that have the same values in some of its columns, the group's key. A model sums over
groups: a machine's hours in a period are a sum over the rows of that machine in that period, and an item's balance in
a period is a sum over the rows of that item in that period. `groupBy` divides a set's rows into groups by the columns
that you name.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket.
- `machines` holds the machines M1 and M2. M1 makes the frame, the plate and the bracket. M2 makes the plate, the
  bolt and the nut.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. A period is an `int`.
- `sPRODUCE` holds each machine, each item that the machine makes and that the plant needs, and each period.
  [Match rows with a set](relational-algebra.md#match-rows-with-a-set) builds it.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns.

The plant needs every item but the bracket. So `sPRODUCE` holds M1 with the frame and the plate, and M2 with the
plate, the bolt and the nut, each in the three periods: 15 rows. The rows start with M1's frame in periods 1, 2 and
3, then M1's plate in each period, and end with M2's nut in period 3.

## Walk the groups

A machine's hours in a period are shared by the items that the machine makes in that period, so a model has one row
of hours for each machine in each period. Suppose you want to know how many items share each machine's hours in each
period. To visit each group, walk the groups that `groupBy` returns with a range-based `for` loop:

```cpp
--8<-- "tests/docs/guide/grouping.cpp:walk"
```

- `sPRODUCE.groupBy<cMachine, cPeriod>()` returns a `Grouped`: the rows of `sPRODUCE`, divided into groups by
  machine and period.
- The loop visits each group. The structured binding names the group's key columns, `m` and `t`, and then the
  group's rows, `rows`.
- `rows` is a `RowSlice`: a view of the group's rows in `sPRODUCE`, which copies no row.
- `std::format` writes the machine as its name, then the period and the number of the group's rows.

The groups come in the order in which their keys first appear in `sPRODUCE`. The rows of M1's frame come first, in
periods 1, 2 and 3, so M1's three groups come first, and then M2's. M1 makes two of the needed items, the frame and
the plate, and M2 makes three. So `loads` starts with `M1 in period 1: 2 items`, and its fourth line is
`M2 in period 1: 3 items`. `loads` has a line for each machine in each period: 6 lines.

For what a walk visits, see [`groupBy`](../reference/indexset.md#indexset-groupby),
[`Grouped`](../reference/grouped.md#grouped) and [`RowSlice`](../reference/grouped.md#rowslice).

## Look a group up by its key

An item's balance in a period adds up what every machine makes of the item in that period. Suppose you want the rows
of one item in one period. To read one group, look it up by its key with `slice`:

```cpp
--8<-- "tests/docs/guide/grouping.cpp:slice"
```

- `sPRODUCE.groupBy<cItem, cPeriod>()` groups the rows by item and period. `madeByItemPeriod` keeps the `Grouped`,
  because the slices that `slice` returns are views into it.
- `slice(items.at("plate"), 2)` returns the rows of the group whose key is the plate and period 2. The key holds the
  columns in the order of `groupBy`.
- `slice` returns an empty slice for a key that no row has.
- `contains(items.at("nut"), 3)` returns true if a group has the key.

The plate is made on M1 and on M2, so `platesIn2` holds two rows, M1's first, as in `sPRODUCE`. The plant does not
need the bracket, so `sPRODUCE` has no row of the bracket, and `bracketsIn2` is empty. M2 makes the nut in every
period, so `nutsIn3` is true. `madeByItemPeriod` has a group for each item of `sPRODUCE` in each period: 4 items in 3
periods, 12 groups. None of them is the bracket's, so a walk visits no group of the bracket.

For the keys that a lookup takes, see [`slice`](../reference/grouped.md#grouped-slice),
[`contains`](../reference/grouped.md#grouped-contains) and [Keys](../reference/keys.md).

## Read a group by its position

Suppose you want the group that a walk visits last, by its position rather than by its key. To read a group by its
position, call `at`:

```cpp
--8<-- "tests/docs/guide/grouping.cpp:at"
```

- `byMachinePeriod.at(byMachinePeriod.size() - 1)` returns the last group. Positions count from 0, in the order in
  which a walk visits the groups.
- `at` returns a `Group`, which holds the group's key in `key` and its rows in `rows`. The structured binding takes the
  machine and the period out of `last.key`.
- `last.rows.size()` returns the number of rows of the group.

`byMachinePeriod` has the 6 groups of the walk above, and the last is M2 in period 3. M2 makes the plate, the bolt and
the nut in period 3, so `itemsMade` is 3. The group's rows are the rows of `sPRODUCE` itself, not copies of them.

For `at`, and the exception that it throws for a position past the last group, see
[`at`](../reference/grouped.md#grouped-at) and [`Group`](../reference/grouped.md#group).

## Put each group's rows together

Suppose you want the rows of `sPRODUCE` period by period: every row of period 1, then every row of period 2, then
every row of period 3. `sPRODUCE` holds its rows item by item, with each item's periods together. To lay a set's rows
out group by group, call `regrouped()`:

```cpp
--8<-- "tests/docs/guide/grouping.cpp:regrouped"
```

`groupBy<cPeriod>()` groups the rows by period, and `regrouped()` returns a new set with the rows of each group
together. The new set does not refer into the `Grouped`, so the `Grouped` needs no variable.

`sPRODUCE_by_period` holds the five rows of period 1 first: M1's frame and plate, then M2's plate, bolt and nut, in the order
of `sPRODUCE`. The rows of period 2 come next, then the rows of period 3. `sPRODUCE_by_period` holds the same rows as
`sPRODUCE` in another order, so `sPRODUCE_by_period == sPRODUCE` is true.

For the order of the new set's rows, see [`regrouped`](../reference/grouped.md#grouped-regrouped).

## Pick the first row of each group

Suppose the plant buys the bolt and the nut from Acme and makes the other items, and you order the items in lots:
one lot for each supplier, and one lot for what the plant makes. You want one variable for each lot, and each item
linked to its lot. A lot has no name of its own, because what defines it is a supplier that a made item does not
have. So name each lot by its first item: the rows of a group keep the order of the set, so the first row of each
group is the same on every run. `cSource` and `cLead` are this page's own, not part of the model:

```cpp
--8<-- "tests/docs/guide/grouping.cpp:first-of-group-columns"
```

```cpp
--8<-- "tests/docs/guide/grouping.cpp:first-of-group"
```

- `cSource` holds a `std::optional<std::string>`: the supplier's name, or no value for an item that the plant makes.
  A `std::optional` has `==` and `std::hash`, so it can be a column, and two rows with no value are equal.
- `select(with_source)` makes `sSOURCED`, each item with its supplier or none, and `groupBy<cSource>()` groups the
  items by their supplier: one group for each lot.
- `lead_of` takes a group and returns the item of its first row, as a `cLead`, with the group's supplier.
  `IndexSet{lots, lead_of}` makes `sLEADS` from the groups: one row for each lot, in the order of the groups.
- `with_lead` looks up the group of an item's supplier with `slice` and returns the item with the first item of that
  group. `select(with_lead)` makes `sLOTS`, each item with its lot, in the order of `sSOURCED`.
- `itemsOfLot` groups the items by their lot, and `indexedName` writes the name of the first lot.

The items are in the order of their names: the bolt, the bracket, the frame, the nut and the plate. The bolt and the
nut come from Acme, so they make one lot, whose first item is the bolt. The bracket, the frame and the plate have no
supplier, and two missing suppliers are equal, so the three make the other lot, whose first item is the bracket.
`sLEADS` holds the bolt and the bracket, and `sLOTS` holds every item with its lot: 5 rows. `boughtTogether` is 2,
and `firstLot` is `Lot_bolt`.

Use a `std::optional` column where having no value is a fact about the row, such as an item that no supplier sells.
Do not use one for a value that is unknown, because two unknown values are not known to be equal. Do not use one for
a row that is not there: leave the row out of the set, or build the set as
[`none()`](../reference/indexset.md#indexset-none).

For what each step takes, see [`groupBy`](../reference/indexset.md#indexset-groupby),
[`front`](../reference/grouped.md#rowslice-front), [`IndexSet{records, row_of}`](../reference/indexset.md#indexset-constructor)
and [`indexedName`](../reference/names.md#indexedname).

## See also

- [Grouped](../reference/grouped.md): every member of `Grouped`, `Group` and `RowSlice`, and the calls that do not
  compile on an rvalue `Grouped`.
- [Ordered sets](ordered-sets.md): positions and neighbours within each group, and an order kept as a member of a
  class, as a `Grouped` is kept.
- [Values over a set](values-over-a-set.md): a value taken over a group's rows.
- [Deriving sets](deriving-sets.md): a set derived from one set.
- Debug-build checks: [`front()` or `back()` of an empty slice](../reference/debug-build-checks.md#slice_has_row).
