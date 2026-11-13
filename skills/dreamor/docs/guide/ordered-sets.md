<!-- Copied from docs/guide/ordered-sets.md (blob f22937627dd0), tests/docs/guide/model.hpp (blob 906d31015f4d), tests/docs/guide/ordered_sets.cpp (blob 05237ba7d71c) by tools/sync_skill_docs.py - edit the source, not this copy. -->

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
const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, p.periods)).build();
const auto sPRODUCE = from<cMachine>(p.machines).lateral<cItem>(items_of).join(sNEEDED).cross(sPERIODS).build();
const auto sSTOCK   = from(sNEEDED).cross(sPERIODS).build();
const auto sCONSUME = from(quantity.keys()).join(sPRODUCE.select(as_parent)).build();
```

The examples use two of the sets. `s.PERIODS` is `sPERIODS`, the periods 1, 2 and 3, in that order. `s.STOCK` is
`sSTOCK`, each item that the plant needs in each period. `sNEEDED` holds the frame, the nut, the plate and the bolt,
as [Find the items the products need](set-algebra.md#find-the-items-the-products-need) shows.

## Read a row's position and the set's ends

A model over periods often needs to know where a period stands: whether it is the first or the last period, and its
place in the plan. To read positions in a set's order, call `ordered()` on the set:

```cpp
const auto orderedPeriods = s.PERIODS.ordered();

const std::optional<std::size_t> position = orderedPeriods.positionOf(2);  // 1: positions count from 0
const auto& [opening]                     = orderedPeriods.first();        // period 1
const auto& [closing]                     = orderedPeriods.last();         // period 3
const bool opensAtOne                     = orderedPeriods.isFirst(1);     // true
const bool closesAtTwo                    = orderedPeriods.isLast(2);      // false
```

- `s.PERIODS.ordered()` returns an `Ordered`, which holds the position of each row of `s.PERIODS`. Positions count
  from 0.
- `positionOf(2)` returns the position of the row whose period is 2. A lookup takes a row's key, here the period's
  number. `positionOf` returns a `std::optional`, which is empty if no row has the key.
- `first()` and `last()` return the first and the last row. A row of `s.PERIODS` is a `std::tuple<cPeriod>`, so a
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
const auto frame = items.at("frame");
std::vector<double> soldBy(periods);  // the frames sold up to period t, at [t - 1]
for (const auto& [t] : s.PERIODS) {
  soldBy[t - 1] = frame->demand[t - 1];
  if (const auto* before = orderedPeriods.prev(t)) {
    const auto& [earlier] = *before;
    soldBy[t - 1] += soldBy[earlier - 1];
  }
}
```

- `items.at("frame")` returns a `Ref` to the frame. `soldBy` holds a value for each period.
- The loop visits each period `t` of `s.PERIODS` in order, and starts the value of `t` with the frames sold in `t`.
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
const auto not_first = [&orderedPeriods](cPeriod t) { return !orderedPeriods.isFirst(t); };

std::vector<std::tuple<cItem, cPeriod, double>> changes;  // the change in demand from the period before
for (const auto& [i, t] : s.STOCK.where(not_first)) {     // each item kept, in every period but the first
  const auto& [earlier] = orderedPeriods.prevOf(t);
  changes.emplace_back(i, t, i->demand[t - 1] - i->demand[earlier - 1]);
}
```

- `not_first` is true for a period that is not the first row of `orderedPeriods`.
- `s.STOCK.where(not_first)` returns the rows of `s.STOCK` in every period but the first. The loop runs over these
  rows.
- `orderedPeriods.prevOf(t)` returns the row before `t` itself, not a pointer to it. Every period that the loop
  visits has a period before it.
- `changes.emplace_back` adds the item, the period and the change in the item's demand.

`s.STOCK` holds the frame, the nut, the plate and the bolt in each period, so the loop visits each of them in periods
2 and 3: 8 changes, starting with the frame's in period 2. The frame's demand goes from 1 to 2 to 2, so its change is
1 in period 2 and 0 in period 3. The nut's demand goes from 0 to 5 to 0, so its change is 5 in period 2 and -5 in
period 3. The plate and the bolt have no demand, so their changes are 0. `changes` holds no change for period 1.

For what `prevOf` requires, see [`prevOf`](../reference/ordered.md#ordered-prevof) and
[`where`](../reference/indexset.md#indexset-where).

## Wrap around the ends

Suppose the plan repeats: after period 3, period 1 comes again, so the period before period 1 is period 3. To read
the row before a row, and wrap from the first row to the last, call `prevCyclic`:

```cpp
std::vector<double> soldBefore(periods);  // the frames sold the period before t; the horizon repeats
for (const auto& [t] : s.PERIODS) {
  const auto& [earlier] = *orderedPeriods.prevCyclic(t);  // before period 1, period 3
  soldBefore[t - 1]     = frame->demand[earlier - 1];
}
```

- `orderedPeriods.prevCyclic(t)` returns a pointer to the row before `t`, and to the last row if `t` is the first.
  It returns a null pointer only if no row has the key, so `*` reads a row for every period of `s.PERIODS`.
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
using dreamor::index_set::relational::from;

const auto items_of = [](cMachine m) -> const auto& { return m->items; };

const auto sMADE          = from<cMachine>(machines).lateral<cItem>(items_of).build();  // each machine's items
const auto itemsOnMachine = sMADE.orderedWithin<cMachine>();

const cMachine m1        = machines.at("M1");
const auto& firstOnM1    = itemsOnMachine.first(m1);                                   // M1's frame
const auto* afterPlate   = itemsOnMachine.next(m1, items.at("plate"));                 // M1's bracket
const auto* afterBracket = itemsOnMachine.next(m1, items.at("bracket"));               // nullptr: M1's last
const bool nutLastOnM2   = itemsOnMachine.isLast(machines.at("M2"), items.at("nut"));  // true
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
// The machines' items and their order on each machine, kept together.
struct Routing {
  dreamor::IndexSet<cMachine, cItem> sMADE;                                       // no rows until assigned
  dreamor::Ordered<dreamor::IndexSet<cMachine, cItem>, cMachine> itemsOnMachine;  // no rows until assigned
};

Routing routingOf(const dreamor::EntityVector<Machine>& machines) {
  using dreamor::index_set::relational::from;
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };

  Routing routing;
  routing.sMADE          = from<cMachine>(machines).lateral<cItem>(items_of).build();
  routing.itemsOnMachine = routing.sMADE.orderedWithin<cMachine>();
  return routing;
}

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
