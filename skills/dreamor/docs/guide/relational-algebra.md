<!-- Copied from docs/guide/relational-algebra.md (blob d6c7397d3131), tests/docs/guide/relational_algebra.cpp (blob ae71944d0ff9) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Relational algebra

A chain builds a set step by step. `from` starts the chain with one source, which fills the first column. A source is
what the chain reads values from, such as an `EntityVector`, a range of integers, a set or values written in braces.
Each step after `from` adds a source or a condition. `build()` then makes the rows and returns them as an `IndexSet`.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket.
- `machines` holds the machines M1 and M2. M1 makes the frame, the plate and the bracket. M2 makes the plate, the
  bolt and the nut.
- `periods` is the number of periods, 3.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. A period is an `int`.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns.

## Build a set from sources and a condition

Suppose you want every combination of a machine, an item that the machine makes, and a period, but only where a batch
of 8 units of the item fits in the machine's hours for one period. Start a chain with `from`, and add one source or
condition at each step:

```cpp
using dreamor::index_set::relational::from;

const auto items_of   = [](cMachine m) -> const auto& { return m->items; };
const auto fits_batch = [](cMachine m, cItem i) { return 8 * i->time <= m->capacity; };  // a batch of 8 units

const auto sBATCH = from<cMachine>(machines)
                        .lateral<cItem>(items_of)
                        .cross<cPeriod>(dreamor::interval::closed(1, periods))
                        .where(fits_batch)
                        .build();
```

- `from<cMachine>(machines)` starts the chain with every machine.
- `lateral<cItem>(items_of)` adds the column `cItem`, with the items that each machine makes. For each machine,
  `build()` calls `items_of`, which returns the machine's list of items, and makes one row for each item in the list.
- `cross<cPeriod>(dreamor::interval::closed(1, periods))` adds the column `cPeriod` and pairs every row with every
  period. `closed(1, periods)` is the integers from 1 to 3, both included.
- `where(fits_batch)` keeps only the rows where 8 units of the item take no more than the machine's hours.
- `build()` makes the rows.

`items_of` and `fits_batch` are column callables: lambdas whose parameters are columns. `build()` calls each one with
those columns of the row. [Write a callable over columns](columns.md#write-a-callable-over-columns) shows how to write
one. The `using` declaration makes `from` available without its namespace, `dreamor::index_set::relational`.

M1 has 4 hours in a period, and M2 has 3. A batch of 8 frames takes 8 hours, so the frame does not fit on M1. A batch
of 8 plates takes 4 hours, so the plate does not fit on M2. `sBATCH` holds M1 with the plate and the bracket, and M2
with the bolt and the nut, each in the three periods: 12 rows. `sBATCH` is an `IndexSet<cMachine, cItem, cPeriod>`,
with its columns in the order in which the steps added them.

For what each step takes, see [`from`](../reference/relational-algebra.md#from-column),
[`lateral`](../reference/relational-algebra.md#chain-lateral), [`cross`](../reference/relational-algebra.md#chain-cross-column),
[`where`](../reference/relational-algebra.md#chain-where), [`build`](../reference/relational-algebra.md#chain-build) and
[`closed`](../reference/intervals.md#closed).

## Fill a column from values written in braces

Suppose each machine is serviced in periods 1 and 3, and you want a row for each machine in each of those periods.
To fill a column with a few values that you know, write the values in braces:

```cpp
const auto sSERVICE = from<cMachine>(machines).cross<cPeriod>({1, 3}).build();  // each machine's service periods
```

`cross<cPeriod>({1, 3})` adds the column `cPeriod` and pairs each machine with each value written.

`sSERVICE` holds M1 and M2, each in periods 1 and 3: 4 rows. It holds no row for period 2.

For the values that braces may hold, see [`cross`](../reference/relational-algebra.md#chain-cross-column).

## Pair every row with a set's rows

Suppose you want a row for each machine in each period, to hold the machine's hours in that period. The periods are
also a set of their own, which other sets use. To pair every row with every row of a set, pass the set to `cross`:

```cpp
const auto sPERIODS  = from<cPeriod>(dreamor::interval::closed(1, periods)).build();
const auto sCAPACITY = from<cMachine>(machines).cross(sPERIODS).build();  // a machine's hours in a period
```

- The first line makes `sPERIODS`, a set of one column, `cPeriod`, with the periods 1, 2 and 3.
- `cross(sPERIODS)` adds the set's column, `cPeriod`, and pairs every machine with every row of the set. The call
  names no column, because the set has its own.

`sCAPACITY` holds each machine in each period: 6 rows. `sCAPACITY` is an `IndexSet<cMachine, cPeriod>`.

`sPERIODS` shares no column with the rows so far. If a set shares a column with the rows so far, use `join`, which
the next section shows. For what `cross` takes, see [`cross`](../reference/relational-algebra.md#chain-cross-rows).

## Match rows with a set

A machine should make only the items that the plant needs. Suppose you want each machine with each item that it
makes and that the plant needs, in each period. `sNEEDED` is the set of the items that the plant needs, of one
column, `cItem`: the items that customers buy, and every part below them.
[Fill one column from a set of another](#fill-one-column-from-a-set-of-another) builds it. To keep the rows that match
a set, pass the set to `join`:

```cpp
const auto sPRODUCE = from<cMachine>(machines).lateral<cItem>(items_of).join(sNEEDED).cross(sPERIODS).build();
```

- `from<cMachine>(machines).lateral<cItem>(items_of)` makes a row for each machine and each item that the machine
  makes.
- `join(sNEEDED)` matches the rows with the rows of `sNEEDED` on the column that they share, `cItem`. A row is kept if
  `sNEEDED` has a row with the same item. The chain has every column of `sNEEDED` already, so the join adds no column.
- `cross(sPERIODS)` pairs every row with every period.

`sNEEDED` holds the frame, the plate, the bolt and the nut, and not the bracket. So `sPRODUCE` holds M1 with the frame
and the plate, and M2 with the plate, the bolt and the nut, each in the three periods: 15 rows. `sPRODUCE` is an
`IndexSet<cMachine, cItem, cPeriod>`, and it holds no row for M1's bracket.

For what `join` takes, see [`join`](../reference/relational-algebra.md#chain-join).

## Fill one column from a set of another

Suppose you want the items that the plant needs: the items that customers buy, and every part that goes into them,
directly or through other parts. A part comes from the bill of materials, which pairs a parent with a part, so the
items move between columns along the way. The model has two more columns, `cParent` and `cChild`, which each hold an
item. `quantity` is the bill of materials, a `ParamTable<double, cParent, cChild>`, and its `keys()` are its lines.
[Data on a set](data-on-a-set.md#make-a-table-from-records) makes it.

```cpp
const auto is_sold = [](cItem i) { return std::ranges::max(i->demand) > 0; };
const auto as_item = [](cChild c) { return cItem{c}; };

const auto sSOLD       = from<cItem>(items).where(is_sold).build();
const auto sBELOW      = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
const auto sBELOW_sold = from<cParent>(sSOLD).join(sBELOW).build();    // a sold item and each part below it
const auto sNEEDED     = dreamor::unite(sSOLD, sBELOW_sold.select(as_item));
```

- `sSOLD` holds the items that customers buy. `is_sold` keeps an item if its demand is above 0 in some period.
- `transitiveClosure(quantity.keys())` returns `sBELOW`, every pair of an item and a part that a path of lines leads
  to. No line has the frame take a nut, but `sBELOW` pairs the frame with the nut, which the frame takes through the
  plate.
- `from<cParent>(sSOLD)` fills `cParent` with the items of `sSOLD`. `sSOLD` holds its items in `cItem`, and
  `cParent` holds an item too, so the items move to `cParent`.
- `join(sBELOW)` matches the rows with the rows of `sBELOW` on `cParent`, and adds `cChild`. `sBELOW_sold` pairs
  each sold item with each part below it.
- `select(as_item)` makes each row of `sBELOW_sold` anew from its `cChild`, as a `cItem`.
- `unite` returns the rows that are in either set.

`sSOLD` holds the frame and the nut. `sBELOW_sold` pairs the frame with the plate, the bolt and the nut. The nut takes
no part, so `sBELOW_sold` pairs it with nothing. `sNEEDED` holds the frame, the nut, the plate and the bolt. The nut
is both sold and a part of the frame, and `sNEEDED` holds it once. The bracket is not in `sNEEDED`, because no
customer buys it and no sold item takes it.

For what each call takes, see [`from`](../reference/relational-algebra.md#from-column),
[`join`](../reference/relational-algebra.md#chain-join),
[`transitiveClosure`](../reference/set-algebra.md#transitiveclosure),
[`select`](../reference/indexset.md#indexset-select-expression), [`unite`](../reference/set-algebra.md#unite) and
[`keys`](../reference/paramtable.md#paramtable-keys). [Find every part below an item](set-algebra.md#find-every-part-below-an-item)
shows the closure on its own.

## Build several sets from one chain

Two sets often start with the same steps. Suppose you want each machine with every item that it makes, and also
with only the items that customers buy. Keep the shared steps in a variable, and build each set from the variable:

```cpp
const auto made       = from<cMachine>(machines).lateral<cItem>(items_of);
const auto sMADE_sold = made.join(sSOLD).build();  // the items customers buy, on the machine that makes them
const auto sMADE      = made.build();              // every item each machine makes
```

- `made` is a chain that is not built yet: every machine, with each item that the machine makes.
- `made.join(sSOLD)` returns a new chain and leaves `made` as it was. `sSOLD`, from the section above, holds the
  items that customers buy.
- `made.build()` makes the rows of `made` itself.

`sMADE_sold` holds M1 with the frame and M2 with the nut. `sMADE` holds every item that each machine makes, 3 on each
machine: 6 rows. The `join` did not change `made`.

For what a step does to a chain in a variable, see [`Chain`](../reference/relational-algebra.md#chain).

## Build the selective part first

Suppose you want each machine with each item that it makes, in each period in which customers buy the item.
Customers buy few items in each period, so the condition on the item and the period keeps few rows. Build those rows
first, from the items and the periods alone, and match the machines with them:

```cpp
const auto has_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };

const auto sSALES = from<cItem>(items).cross(sPERIODS).where(has_demand).build();  // the few rows customers buy
const auto sSERVE = from<cMachine>(machines).lateral<cItem>(items_of).join(sSALES).build();
```

- `has_demand` is true if customers buy the item in the period.
- `sSALES` pairs every item with every period, and keeps the rows for which `has_demand` is true.
- The chain of `sSERVE` makes a row for each machine and each item that the machine makes. `join(sSALES)` matches
  the rows with the rows of `sSALES` on `cItem`, and adds `cPeriod`.

`sSALES` holds the frame in each period and the nut in period 2: 4 rows. `sSERVE` holds M1 with the frame in each
period and M2 with the nut in period 2: 4 rows. One chain that ends in `cross(sPERIODS).where(has_demand)` makes the
same rows.

`build()` reads the sources as nested loops, one loop for each source. The order of the loops is the visit order.
`build()` chooses the visit order when the program compiles, so the order cannot depend on how many rows a condition
keeps. In the one chain, the item comes from its machine's list, so `has_demand` runs on each machine's item in each
period. In the code above, `has_demand` runs on each item in each period, and the chain of `sSERVE` matches the
machines' items with the 4 rows of `sSALES` alone.

For how `build()` chooses the visit order, see the introduction of
[Relational algebra](../reference/relational-algebra.md) and [`build`](../reference/relational-algebra.md#chain-build).

## Log the order a chain visits its sources in

To see in which order `build()` reads a chain's sources, log the visit order. The order shows whether a set that you
built first, such as `sSALES`, is read where you expect:

```cpp
const auto serve = from<cMachine>(machines).lateral<cItem>(items_of).join(sSALES);
DREAMOR_LOG_INFO("sSERVE visits {}", serve.visitOrder());  // cMachine, cItem, {cItem, cPeriod}
```

- `serve` is the chain of `sSERVE` from the section above, not built.
- `serve.visitOrder()` returns the visit order as text: the columns of each source, in the order in which `build()`
  reads the sources. The columns of a set are in braces.
- `DREAMOR_LOG_INFO` writes the text to dreamor's log as an information line.

The log holds the line `sSERVE visits cMachine, cItem, {cItem, cPeriod}`: `build()` reads the machines, then each
machine's items, then the rows of `sSALES`. `serve.build()` makes the same rows as `sSERVE`. `visitOrder()` is a
`constexpr` function, so a `static_assert` can hold a model to its visit order.

For what `visitOrder()` returns and how a log line is written, see
[`visitOrder`](../reference/relational-algebra.md#chain-visitorder) and
[`DREAMOR_LOG_INFO`](../reference/logger.md#dreamor_log_info).

## See also

- [Relational algebra](../reference/relational-algebra.md): every step of a chain.
- [Set-builder notation](set-builder-notation.md): the same sets, written with `of` and `in`.
- [Sets from records](sets-from-records.md): a set made from the records that hold a relation.
- [Set algebra](../reference/set-algebra.md): union, intersection, difference, transitive closure.
- Debug-build checks: [a source holds no element twice](../reference/debug-build-checks.md#source_holds_no_repeat),
  [nor an empty `Ref`](../reference/debug-build-checks.md#source_holds_no_empty_ref),
  [an integer fits its column](../reference/debug-build-checks.md#column_integer_in_range).
