# Ordered sets

A set holds its rows in a fixed order: the order in which they were made. The periods made from an interval come 1,
2, 3, and a machine's items come in the order in which the machine's `items` lists them. A model over time reads that
order: the first period, the period before a period, the last period. `ordered()` returns the positions of a set's
rows in that order, and `orderedWithin` returns the positions within each group of rows. To give a set another order
first, sort it with `orderBy`, as [Sort the rows](deriving-sets.md#sort-the-rows) shows.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket.
- `machines` holds the machines M1 and M2. M1's `items` lists the frame, the plate and the bracket. M2's `items`
  lists the plate, the bolt and the nut.
- `periods` is the number of periods, 3.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. A period is an `int`.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns.

`s` holds the model's sets, which `byChain` builds with these chains:

```cpp
--8<-- "tests/docs/guide/model.hpp:chain"
```

The examples use two of the sets. `s.periods` is `sPERIODS`, the periods 1, 2 and 3, in that order. `s.stock` is
`sSTOCK`, each item that the plant needs in each period. `sNEEDED` holds the frame, the nut, the plate and the bolt,
as [Find the items the products need](set-algebra.md#find-the-items-the-products-need) shows.

## Read a row's position and the set's ends

A model over periods often needs to know where a period stands: whether it is the first or the last period, and its
place in the plan. To read positions in a set's order, call `ordered()` on the set:

```cpp
--8<-- "tests/docs/guide/ordered_sets.cpp:positions"
```

- `s.periods.ordered()` returns an `Ordered`, which holds the position of each row of `s.periods`. Positions count
  from 0.
- `positionOf(2)` returns the position of the row whose period is 2. A lookup takes a row's key, here the period's
  number. `positionOf` returns a `std::optional`, which is empty if no row has the key.
- `first()` and `last()` return the first and the last row. A row of `s.periods` is a `std::tuple<cPeriod>`, so a
  structured binding takes the period out of it.
- `isFirst(1)` and `isLast(2)` check whether the row of a period is the first or the last row.

Period 2 is the second row, so `position` is 1. `opening` is period 1, and `closing` is period 3. `opensAtOne` is
true. `closesAtTwo` is false, because the last period is 3.

For what each lookup takes, see [`Ordered`](../reference/ordered.md#ordered),
[`positionOf`](../reference/ordered.md#ordered-positionof), [`first`](../reference/ordered.md#ordered-first),
[`last`](../reference/ordered.md#ordered-last), [`isFirst`](../reference/ordered.md#ordered-isfirst) and
[`isLast`](../reference/ordered.md#ordered-islast).

## Read the period before, where there may be none

The frames sold up to a period are the frames sold in that period and the frames sold up to the period before it.
Period 1 has no period before it. To read the row before a row, where there may be none, call `prev`:

```cpp
--8<-- "tests/docs/guide/ordered_sets.cpp:prev"
```

- `items.at("frame")` returns a `Ref` to the frame. `soldBy` holds a value for each period.
- The loop visits each period `t` of `s.periods` in order, and starts the value of `t` with the frames sold in `t`.
- `orderedPeriods.prev(t)` returns a pointer to the row before `t`, or a null pointer if `t` is the first row. So the
  `if` adds the frames sold up to the period before only where there is a period before.
- `const auto& [earlier] = *before;` takes the period out of the row.

The frame's demand is 1, 2 and 2 in the three periods. Period 1 has no period before it, so its value stays 1.
Period 2 adds its 2 to 1, and period 3 adds its 2 to 3: `soldBy` is 1, 3 and 5.

For `prev`, and for `next`, which reads the row after a row, see [`prev`](../reference/ordered.md#ordered-prev) and
[`next`](../reference/ordered.md#ordered-next).

## Write a row only where the period before is

Suppose you want the change in each needed item's demand from the period before. Period 1 has no period before it,
so it has no change. Leave period 1 out of the set that the loop runs over, and read the period before with
`prevOf`:

```cpp
--8<-- "tests/docs/guide/ordered_sets.cpp:where-neighbour"
```

- `not_first` is true for a period that is not the first row of `orderedPeriods`.
- `s.stock.where(not_first)` returns the rows of `s.stock` in every period but the first. The loop runs over these
  rows.
- `orderedPeriods.prevOf(t)` returns the row before `t` itself, not a pointer to it. Every period that the loop
  visits has a period before it.
- `changes.emplace_back` adds the item, the period and the change in the item's demand.

`s.stock` holds the frame, the nut, the plate and the bolt in each period, so the loop visits each of them in periods
2 and 3: 8 changes, starting with the frame's in period 2. The frame's demand goes from 1 to 2 to 2, so its change is
1 in period 2 and 0 in period 3. The nut's demand goes from 0 to 5 to 0, so its change is 5 in period 2 and -5 in
period 3. The plate and the bolt have no demand, so their changes are 0. `changes` holds no change for period 1.

For what `prevOf` requires, see [`prevOf`](../reference/ordered.md#ordered-prevof) and
[`where`](../reference/indexset.md#indexset-where).

## Wrap around the ends

Suppose the plan repeats: after period 3, period 1 comes again, so the period before period 1 is period 3. To read
the row before a row, and wrap from the first row to the last, call `prevCyclic`:

```cpp
--8<-- "tests/docs/guide/ordered_sets.cpp:cyclic"
```

- `orderedPeriods.prevCyclic(t)` returns a pointer to the row before `t`, and to the last row if `t` is the first.
  It returns a null pointer only if no row has the key, so `*` reads a row for every period of `s.periods`.
- `soldBefore[t - 1]` takes the frames sold in the period before `t`.

Before period 1 comes period 3, in which 2 frames are sold. Before period 2 comes period 1, with 1 frame, and before
period 3 comes period 2, with 2 frames. `soldBefore` is 2, 1 and 2.

For `prevCyclic`, and for `nextCyclic`, which wraps from the last row to the first, see
[`prevCyclic`](../reference/ordered.md#ordered-prevcyclic) and
[`nextCyclic`](../reference/ordered.md#ordered-nextcyclic).

## Order the rows within each group

A machine works through its items in the order in which its `items` lists them. Suppose you want the first item of a
machine, the item after another on the same machine, and whether an item is a machine's last. Each machine's list is
a sequence of its own, so M1's last item has no item after it. To read positions within each group of rows, call
`orderedWithin` with the columns that the rows of a group share:

```cpp
--8<-- "tests/docs/guide/ordered_sets.cpp:within"
```

- `sMADE` holds each machine with each item that the machine makes, in the order of the machine's `items`.
- `sMADE.orderedWithin<cMachine>()` returns an `Ordered` whose positions count within the rows of each machine.
- `first(m1)` returns M1's first row. A lookup of a group takes the group's key, here the machine.
- `next(m1, items.at("plate"))` returns a pointer to the row after M1's plate among M1's rows. A lookup of a row takes
  the row's key, the machine and the item.
- `isLast` checks whether a row is the last row of its machine.

`sMADE` holds M1's frame, plate and bracket, then M2's plate, bolt and nut. `firstOnM1` is M1's frame, and
`afterPlate` points to M1's bracket. The bracket is M1's last item, so `afterBracket` is a null pointer, although
M2's plate comes after it in `sMADE`. The nut is M2's last item, so `nutLastOnM2` is true.

For what each lookup takes within groups, see [`orderedWithin`](../reference/indexset.md#indexset-orderedwithin),
[`first`](../reference/ordered.md#ordered-first), [`next`](../reference/ordered.md#ordered-next) and
[`isLast`](../reference/ordered.md#ordered-islast).

## Keep an order as a member

A model that reads one order in several places makes it once and keeps it. `Ordered` is a type you can name,
`dreamor::Ordered<Set, Within...>`, so a class can hold one beside its set and fill both later:

```cpp
--8<-- "tests/docs/guide/ordered_sets.cpp:member"
```

- `Routing` holds `sMADE`, from the section above, and `itemsOnMachine`, its order within each machine. The type of
  `itemsOnMachine` names the set's type and the column whose groups it orders within, as `orderedWithin<cMachine>()`
  returns it.
- Neither member has an initializer, so each starts empty: a set with no rows and an `Ordered` over no rows.
- `routingOf` assigns `sMADE` its rows, then `itemsOnMachine` the order of those rows.

The `Routing` that `routingOf` returns holds the 6 rows of `sMADE` and their order, and its `itemsOnMachine` answers
the lookups of the section above. A `Grouped` is kept the same way, `dreamor::Grouped<Set, Columns...>`, for the
groups that `groupBy` returns.

For the type and its constructors, see [`Ordered`](../reference/ordered.md#ordered) and
[`Grouped`](../reference/grouped.md#grouped).

## See also

- [Ordered](../reference/ordered.md): every member of `Ordered`, and the lookups that do not compile on an rvalue
  `Ordered`.
- [Deriving sets](deriving-sets.md): `orderBy`, which sorts a set into another order.
- [Grouping](grouping.md): a set's rows divided into groups.
- Debug-build checks: [`first()` or `last()` of an empty set](../reference/debug-build-checks.md#ordered_set_has_row),
  [of a group with no row](../reference/debug-build-checks.md#ordered_group_has_row),
  [`nextOf` or `prevOf` past an end](../reference/debug-build-checks.md#ordered_neighbour_is_there).
