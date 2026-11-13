<!-- Copied from docs/guide/data-on-a-set.md (blob 9583519179da), tests/docs/guide/data_on_a_set.cpp (blob 8391c8096309), tests/docs/guide/model.hpp (blob b2d1db7441b4) by tools/sync_skill_docs.py - edit the source, not this copy. -->

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
using dreamor::index_set::relational::from;
const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, periods)).build();
const auto sPLAN    = from<cItem>(items).cross(sPERIODS).build();        // each item in each period
const auto sSHIFTS  = from<cMachine>(machines).cross(sPERIODS).build();  // each machine in each period

const auto demand_of = [](cItem i, cPeriod t) { return i->demand[t - 1]; };
const dreamor::ParamTable<double, cItem, cPeriod> demand{sPLAN, demand_of};
const dreamor::ParamTable<double, cMachine, cPeriod> overtime{sSHIFTS, 2.0};  // hours a machine may add
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

## Make a table from records

The bill of materials comes as records, one for each line: a parent, a part of it, and the units of the part that a
unit of the parent takes. `p` is the plant, and `p.bom` holds its records, which
[Sets from records](sets-from-records.md) shows. Your model needs the quantity of each line, looked up by the parent
and the part. To make the set and the values together, make the table from the records:

```cpp
using BomTable = dreamor::ParamTable<double, cParent, cChild>;  // a line's quantity, by its parent and child

inline BomTable bomTable(const Plant& p) {
  const auto edge_of = [&items = p.items](const BomRecord& r) {
    return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
  };
  const auto quantity_of = [](const BomRecord& r) { return r.quantity; };
  return BomTable{p.bom, edge_of, quantity_of};
}

```

```cpp
const BomTable quantity                        = bomTable(p);
const dreamor::IndexSet<cParent, cChild>& sBOM = quantity.keys();  // a row for each line of the BOM
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
const auto frame = items.at("frame");
const auto plate = items.at("plate");

const double plates = quantity(cParent{frame}, cChild{plate});                     // a frame takes 2 plates
const bool takesNut = quantity.contains(cParent{frame}, cChild{items.at("nut")});  // false: its plates do

const std::tuple<cParent, cChild> line{frame, plate};
const double same = quantity(line);  // a row of the set reads as its key
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
std::vector<std::string> lines;
for (const auto& [row, units] : quantity) {
  const auto& [parent, child] = row;
  lines.push_back(std::format("{} takes {} {}", parent, units, child));
}
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
struct Route {   // making an item on a machine
  double cost;   // a unit's running cost
  double scrap;  // the share of units scrapped
};

struct RouteRecord {
  std::string machine, item;
  double cost, scrap;
};

```

```cpp
const std::vector<RouteRecord> routes{
    {"M1", "frame", 6.0, 0.01},
    {"M1", "plate", 3.0, 0.02},
    {"M2", "plate", 2.5, 0.05},
};
const auto route_key = [&machines, &items](const RouteRecord& r) {
  return std::tuple<cMachine, cItem>{machines.at(r.machine), items.at(r.item)};
};
const auto route_of = [](const RouteRecord& r) { return Route{r.cost, r.scrap}; };
const dreamor::ParamTable<Route, cMachine, cItem> route{routes, route_key, route_of};

const Route& plateOnM2 = route(machines.at("M2"), items.at("plate"));  // cheaper than on M1, more scrap
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
const auto has_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };
const auto sSALES     = from(sPLAN).where(has_demand).build();  // the rows customers buy

const dreamor::ParamTable<double, cItem, cPeriod> orders{sSALES, demand};  // demand's value on each
const double unitsSold = dreamor::sumValue(sSALES, demand);
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
