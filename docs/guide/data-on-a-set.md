# Data on a set

A model reads data that belongs to the rows of a set, such as each item's demand in each period, or the quantity on
each line of the bill of materials. A `ParamTable` holds such data: one value for each row of a set. You look a value
up by the key of its row, as you look a row up in the set. `ParamTable<double, cItem, cPeriod>` holds a `double` for
each row of a set whose columns are `cItem` and `cPeriod`, in that order.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket. Each item has its customers'
  demand in each period. Customers buy frames in every period and nuts in period 2, and nothing else.
- `machines` holds the machines M1 and M2.
- `periods` is the number of periods, 3.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. `cParent` and `cChild` are
  two more columns of items, for the item that takes a part and the part that it takes.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns.

## Put a value on each row

Your model needs the customers' demand for each item in each period, and the extra hours that each machine may add in
a period. Make the set of the rows first, and then a table on the set, with the value of each row:

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:on-each-row"
```

- `sPLAN` pairs every item with every period, and `sSHIFTS` pairs every machine with every period.
  [Relational algebra](relational-algebra.md) shows how a chain builds them.
- `demand_of` returns an item's demand in a period. The demand of period `t` is at `t - 1` in the item's `demand`.
- `demand{sPLAN, demand_of}` makes a table on `sPLAN`. For each row, the table calls `demand_of` with the row's item
  and period, and keeps the value that `demand_of` returns.
- `overtime{sSHIFTS, 2.0}` makes a table on `sSHIFTS` that gives every row the same value.

`demand` holds a value for each item in each period: 5 items in 3 periods, 15 rows. The frame's demand is 1, 2 and 2 in
periods 1, 2 and 3, the nut's is 5 in period 2, and the bracket's is 0 in every period, because no customer buys it.
`overtime` holds 2 hours for each machine in each period: 2 machines in 3 periods, 6 rows.

A table that a class holds can also start empty, declared with no arguments, and be assigned a table later, as a set
can.

For what a table's value may be, and what the constructor takes, see
[`ParamTable`](../reference/paramtable.md#paramtable) and its
[constructor](../reference/paramtable.md#paramtable-constructor).

## Let the compiler name a table's type

The type of a table repeats what its set and its value already say. Leave it out, and the table takes its key columns
from the set and its value type from the value:

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:deduced"
```

- `demand` and `overtime` are the tables of the section above, with their types left out. `sPLAN` gives `demand` its
  key columns, `cItem` and `cPeriod`, and `demand_of`, which returns a `double`, gives its value type.
- `crew{sSHIFTS, 2}` holds an `int` for each row, because `2` is an `int`.
- `shift{sSHIFTS, "day"}` holds a `std::string_view` for each row. A string literal lives as long as the program, so
  a view of it stays valid.

Each table holds the rows and values that it holds with its type named. The type is the argument's own, as with
`std::tuple`'s deduction, not the one your model may mean: write `2.0` for hours, or name the type,
`ParamTable<double, cMachine, cPeriod> crew{sSHIFTS, 2}`, where you want another. Text whose life the table cannot
know - a `const char*`, or a buffer of `char` - gives a table of `std::string`, a copy; so does a callable that
returns `const std::string&`. Name `std::string_view` to keep views of text that outlives the table. A
table made from records, as in the next section, does not deduce its type: name it there.

For the rule, see the [constructor](../reference/paramtable.md#paramtable-constructor).

## Make a table from records

The bill of materials comes as records, one for each line: a parent, a part of it, and the units of the part that a
unit of the parent takes. `p` is the plant, and `p.bom` holds its records, which
[Sets from records](sets-from-records.md) shows. Your model needs the quantity of each line, looked up by the parent
and the part. To make the set and the values together, make the table from the records:

```cpp
--8<-- "tests/docs/guide/model.hpp:bom-table"
```

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:bom-keys"
```

- `BomTable` is a `ParamTable<double, cParent, cChild>`: a quantity for each pair of a parent and a part.
- `edge_of` returns a record's row: a `Ref` to the parent in `cParent`, and a `Ref` to the part in `cChild`.
  `items.at` looks each item up by its name.
- `quantity_of` returns a record's quantity.
- `BomTable{p.bom, edge_of, quantity_of}` makes one row and one value for each record of `p.bom`.
- `bomTable(p)` returns the table, and `quantity.keys()` returns the set that the table is on.

`quantity` holds the six lines of the bill of materials, in the order of the records, each with its line's quantity.
`sBOM` is the set of those six rows.

For what `edge_of` and `quantity_of` may return, see the
[constructor](../reference/paramtable.md#paramtable-constructor) and [`keys`](../reference/paramtable.md#paramtable-keys).
To make the set alone, without the values, see [Sets from records](sets-from-records.md).

## Read a value

A row of the model needs one quantity of the bill of materials, such as how many plates a frame takes. `quantity`,
from the section above, holds the bill of materials. To read the value of a row, call the table with the row's key:

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:read"
```

- `quantity(cParent{frame}, cChild{plate})` returns the value of the row that the key identifies. The key has the
  table's columns in the table's order: the parent, then the part. `cParent` and `cChild` both hold an item, so the
  key names the column of each `Ref`.
- `quantity.contains(...)` returns whether the table's set has the row.
- `quantity(line)` takes a row of the set, a `std::tuple<cParent, cChild>`, as the key.

`plates` is 2, because a frame takes 2 plates. `takesNut` is false: no line of the bill of materials has the frame
take a nut. The frame takes plates, and each plate takes nuts. `same` is 2, the value of the same row as `plates`.

For what a key may be, and what the call throws if the set has no row with the key, see
[`operator()`](../reference/paramtable.md#paramtable-operator-call),
[`contains`](../reference/paramtable.md#paramtable-contains) and [Keys](../reference/keys.md).

## Walk the rows and their values

To write the bill of materials out line by line, walk the table. The walk gives each row with its value:

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:walk"
```

- The range-for gives each row of `quantity` with its value, in the set's order.
- `const auto& [parent, child] = row` takes the row's two columns apart.
- `std::format` writes each column as the name of its item.

`lines` holds one text for each of the six lines, in the order of the records. It starts with
`frame takes 2 plate` and ends with `bracket takes 2 bolt`.

For what the walk gives, see [`begin`](../reference/paramtable.md#paramtable-begin).

## Hold several values a row

Suppose that making an item on a machine has a running cost for each unit and a share of the units that are scrapped,
and that one record holds both. Your model reads both values of a row together. Make a struct of the row's values,
and use it as the table's value. `Route` and `RouteRecord` are this page's own types, not part of the model:

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:route"
```

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:several-values"
```

- `routes` holds three records: the frame on M1, the plate on M1, and the plate on M2.
- `route_key` returns a record's row, the machine and the item, looked up by their names.
- `route_of` returns a `Route` of the record's cost and scrap.
- `route{routes, route_key, route_of}` makes one row and one `Route` for each record.
- `route(machines.at("M2"), items.at("plate"))` returns the `Route` of the plate on M2.

`route` holds 3 rows, one for each record. On M2, a plate costs 2.5 and 5% of the plates are scrapped. On M1, a plate
costs 3.0 and 2% are scrapped. So M2 makes a plate for less, but scraps more of them. `route` has no row for the frame
on M2, because no record names it.

For the types that a table's value may have, see [`ParamTable`](../reference/paramtable.md#paramtable).

## Hand a table where a callable goes

Suppose you want the orders, which are the demand on the rows where customers buy, and the total of the units that
they buy. `demand`, from the first section, holds each item's demand in each period. A `ParamTable` is a column
callable over its columns: called with an item and a period, `demand` returns the demand. So you can pass the table
wherever a column callable goes:

```cpp
--8<-- "tests/docs/guide/data_on_a_set.cpp:hand-on"
```

- `from(sPLAN)` starts a chain with the rows of `sPLAN`, and `where(has_demand)` keeps the rows where customers buy.
- `orders{sSALES, demand}` makes a table on `sSALES`. For each row, the table calls `demand` with the row's item and
  period.
- `dreamor::sumValue(sSALES, demand)` adds up the values of `demand` on the rows of `sSALES`.

`sSALES` holds the frame in each period and the nut in period 2: 4 rows. `orders` holds the demand on those rows: 1, 2
and 2 frames, and 5 nuts. It has no row for the nut in period 1, nor for the bracket. `unitsSold` is 10, the frames'
1 + 2 + 2 and the nuts' 5.

For `sumValue`, see [Values over a set](values-over-a-set.md). For the other places where a table goes as a callable,
such as a bound of a variable family, see the introduction of [ParamTable](../reference/paramtable.md).
[Variable families](variable-families.md) takes a bound from a table, and [Solutions](solutions.md) carries the values
of a solve into the next model through one.

## See also

- [ParamTable](../reference/paramtable.md): every member, and what a value may be.
- [Sets from records](sets-from-records.md): the set that a table's records make.
- [Values over a set](values-over-a-set.md): `sumValue` and the other values computed over a set's rows.
- [Variable families](variable-families.md): a table as a bound.
- Debug-build checks, for a table made from records:
  [two records with one row](../reference/debug-build-checks.md#source_holds_no_repeat),
  [an empty `Ref` in a row](../reference/debug-build-checks.md#source_holds_no_empty_ref).
