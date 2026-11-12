<!-- Copied from docs/guide/columns.md (blob 621f06a29b6c), tests/docs/guide/model.hpp (blob b2d1db7441b4), tests/docs/guide/columns.cpp (blob 7d4f35934698) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Columns

A column is a named place in the rows of a set, such as the machine or the period of a row. A column holds one kind
of value. An entity column holds a `Ref` to an entity, such as a machine. A value column holds a plain value, such as
the number of a period. Each column is a type of its own, so code that reads a row names the columns that it reads,
never their positions in the row.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `p` is the plant. `p.items` holds the items: the frame, the plate, the bolt, the nut and the bracket. `p.machines`
  holds the machines M1 and M2. M1 makes the frame, the plate and the bracket. M2 makes the plate, the bolt and the
  nut. Some examples call the two vectors `items` and `machines`.
- `p.periods` is the number of periods, 3.

[Entities](entities.md) shows the items and the machines.

## Declare the model's columns

The model's sets hold rows such as a machine, an item and a period, so each row needs a place for a machine, a place
for an item and a place for a period. Declare a column for each place, once:

```cpp
DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_ENTITY_COLUMN(cParent, Item);  // an item that takes another
DREAMOR_ENTITY_COLUMN(cChild, Item);   // an item another takes
DREAMOR_VALUE_COLUMN(cPeriod, int);
```

- `DREAMOR_ENTITY_COLUMN(cMachine, Machine)` declares `cMachine`, an entity column that holds a `Ref<Machine>`.
  `cItem`, `cParent` and `cChild` each hold a `Ref<Item>`.
- `DREAMOR_VALUE_COLUMN(cPeriod, int)` declares `cPeriod`, a value column that holds an `int`.

The model has five columns: one for a machine, three for an item, and one for a period. Each column is a type, so
`cItem`, `cParent` and `cChild` are three different types, though each of them holds a `Ref<Item>`.
[Give one entity two columns](#give-one-entity-two-columns) shows why the model needs `cParent` and `cChild`.

For what each macro declares, see [`DREAMOR_ENTITY_COLUMN`](../reference/columns.md#dreamor_entity_column) and
[`DREAMOR_VALUE_COLUMN`](../reference/columns.md#dreamor_value_column).

## Use a column as what it holds

A condition or a sum gets the columns of a row, and it needs what they hold: the hours of the row's machine, or the
number of the row's period. Use each column as the value that it holds:

```cpp
const cMachine m = machines.at("M1");
const cPeriod t{2};
const double hours = m->capacity;
const cPeriod next = t + 1;
const bool late    = t > 1;

const int start = std::max(t.get(), 3);  // std::max deduces one type
const int shown = late ? t.get() : 0;    // ?: takes one type
```

- `machines.at("M1")` returns a `Ref` to M1, and the `Ref` converts to the column `m`. `cPeriod t{2}` makes a column
  that holds period 2.
- `m->capacity` reads a member of the machine that `m` refers to.
- `t + 1` and `t > 1` read `t` as the `int` that it holds. The sum, an `int`, converts back to a `cPeriod`.
- `t.get()` returns the `int` that `t` holds. In `std::max` and in `?:`, C++ needs one type for both operands, and it
  finds none for a column and an `int`, so pass `t.get()` there.

`hours` is 4, M1's hours in a period. `next` holds period 3, and `late` is true. `start` is 3, the greater of 2 and
3, and `shown` is 2.

For how a column converts and compares, see [`operator ReadType`](../reference/columns.md#col-operator-readtype),
[`get`](../reference/columns.md#col-get), [`operator->`](../reference/columns.md#col-operator-arrow) and
[`operator<=>`](../reference/columns.md#col-operator-spaceship).

## Give one entity two columns

A line of the bill of materials holds two items: the parent, which takes the other item, and the child, which goes
into the parent. A row that holds both items needs a column for each, to tell which item is which. `cParent` and
`cChild`, from the first section, each hold a `Ref<Item>`:

```cpp
const cParent parent = items.at("frame");
const cChild child   = items.at("plate");
const cItem item{child};  // the plate, as an item
```

- `parent` is the frame as a `cParent`, and `child` is the plate as a `cChild`. A `Ref<Item>` converts to either
  column.
- `cItem item{child}` makes a `cItem` from `child`. A column converts to a column of another name only explicitly,
  in braces as here.

`parent` and `child` are of two types, so a set or a callable tells the parent from the child by the column's type.
`item` refers to the plate, and it equals `child`: two columns of different names compare by the values that they
hold.

For how one column is made from another, see [`Col`](../reference/columns.md#col-constructor) and
[`operator==`](../reference/columns.md#col-operator-eq).

## Write a callable over columns

Suppose you want each machine with each item that it makes, in each period, where a batch of 8 units of the item
fits in the machine's hours for one period. Whether a batch fits depends on the row's item and machine alone. Write
the condition as a column callable: a lambda whose parameters are the columns that it reads.

```cpp
const auto items_of = [](cMachine m) -> const auto& { return m->items; };
const auto fits     = [](const cItem& i, cMachine m) { return 8 * i->time <= m->capacity; };  // a batch of 8

const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, p.periods)).build();
const auto sMAKE    = from<cMachine>(p.machines).lateral<cItem>(items_of).where(fits).cross(sPERIODS).build();
```

- `items_of` has one parameter, a `cMachine`, and returns the items that the machine makes.
- `fits` has two parameters: a `cItem`, taken by `const&`, and a `cMachine`, taken by value. It returns true if 8
  units of the item take no more than the machine's hours.
- `sPERIODS` is a set of one column, `cPeriod`, with the periods from 1 to 3.
- The chain of `sMAKE` starts with every machine. `lateral<cItem>(items_of)` adds the column `cItem`, with the items
  that `items_of` returns for each machine. `where(fits)` keeps the rows for which `fits` returns true.
  `cross(sPERIODS)` pairs every row with every period, and `build()` makes the rows.

`build()` calls each callable with the columns that its parameters name, taken from the row. The rows hold `cMachine`
before `cItem`, and `fits` names `cItem` first: the parameters may come in any order.

A batch of 8 frames takes 8 hours, more than M1's 4. A batch of 8 plates takes 4 hours, more than M2's 3. `sMAKE`
holds M1 with the plate and the bracket, and M2 with the bolt and the nut, each in the three periods: 12 rows.

For what a column callable may be, see [Column callables](../reference/column-callables.md).
[Relational algebra](relational-algebra.md) shows each step of a chain.

## Look a row up by its key

A constraint often depends on whether a set has a row, for example whether M2 can make bolts in period 3. To look a
row up, pass its key: a value for each column of the set, in the set's order. `sMAKE`, from the section above, holds
each machine with each item of which a batch fits, in each period.

```cpp
const cMachine m2 = machines.at("M2");
const cChild nut  = items.at("nut");

const bool makesBolt = sMAKE.contains(m2, items.at("bolt"), 3);  // a column, a Ref, an int
const bool makesNut  = sMAKE.contains(m2, nut, 3);               // cChild in cItem's place

const auto byMachinePeriod = sMAKE.groupBy<cMachine, cPeriod>();
const std::size_t onM2     = byMachinePeriod.slice(m2, 1).size();
```

- `sMAKE.contains(m2, items.at("bolt"), 3)` checks whether `sMAKE` has the row of M2, the bolt and period 3. Each
  argument fills one column: `m2` is a `cMachine`, the `Ref<Item>` fills `cItem`, the one column of `sMAKE` that holds
  an item, and the `int` 3 fills `cPeriod`.
- `sMAKE.contains(m2, nut, 3)` passes `nut`, a `cChild`, in the place of `cItem`. A column of another name fills the
  column that holds the same type.
- `sMAKE.groupBy<cMachine, cPeriod>()` groups the rows of `sMAKE` by their machine and their period. `slice(m2, 1)`
  returns the rows of M2 in period 1, and it takes its key as `contains` does.

`makesBolt` and `makesNut` are true: M2 makes the bolt and the nut, and a batch of 8 of each fits in its hours.
`onM2` is 2: in period 1, M2 has the rows of the bolt and the nut, and no row of the plate.

For every argument that a key takes, see [Keys](../reference/keys.md). [Grouping](grouping.md) shows `groupBy` and
`slice`.

## Name the columns where two hold one type

Suppose you want the number of plates that a frame takes. `quantity` holds it: it is the bill of materials as a
table, with the quantity on each line, by the line's parent and child. [Data on a set](data-on-a-set.md#make-a-table-from-records)
makes `quantity`. Both columns of its key hold an item, so a `Ref` alone does not say which column it fills. Name the
column of each `Ref`:

```cpp
const dreamor::Ref<Item> frame = items.at("frame");
const dreamor::Ref<Item> plate = items.at("plate");

const double plates = quantity(cParent{frame}, cChild{plate});  // a frame takes 2 plates
const bool listed   = quantity.contains(cParent{plate}, cChild{frame});
```

- `cParent{frame}` and `cChild{plate}` make the two columns of the key from the two `Ref`s.
- `quantity(cParent{frame}, cChild{plate})` returns the quantity on the line whose parent is the frame and whose
  child is the plate.
- `quantity.contains(cParent{plate}, cChild{frame})` checks whether a line has the plate as its parent and the frame
  as its child.

`plates` is 2, because a frame takes 2 plates. `listed` is false: the plate takes bolts and nuts, and no line has a
plate take a frame. The same two items in the other columns are another key.

For the arguments that a key takes where two of its columns hold one type, see [Keys](../reference/keys.md). For a
table's lookups, see [`operator()`](../reference/paramtable.md#paramtable-operator-call) and
[`contains`](../reference/paramtable.md#paramtable-contains).

## Declare a column of an enum or of text

Suppose that each machine runs in a day shift or a night shift, and that your data names the supplier of a bought
item by its name. A value column holds an enum or a text as it holds an integer. `Shift`, `cShift` and `cSupplier`
are this page's own, not part of the model:

```cpp
enum class Shift { kDay, kNight };

DREAMOR_VALUE_COLUMN(cShift, Shift);           // the shift a machine runs in
DREAMOR_VALUE_COLUMN(cSupplier, std::string);  // who supplies a bought item, by name
```

```cpp
const cShift shift{Shift::kNight};
const cSupplier supplier{"Acme"};

const bool atNight  = shift == Shift::kNight;
const bool fromAcme = supplier == "Acme";
```

- `DREAMOR_VALUE_COLUMN(cShift, Shift)` declares a column that holds a `Shift`, an enum.
- `DREAMOR_VALUE_COLUMN(cSupplier, std::string)` declares a column that holds a `std::string`.
- `shift == Shift::kNight` compares the value of `shift` with an enumerator.
- `supplier == "Acme"` compares the text of `supplier` with a string literal, character by character.

`atNight` and `fromAcme` are true.

For every type that a value column may hold, see [`Col`](../reference/columns.md#col).

## See also

- [Columns](../reference/columns.md): every member of a column.
- [Keys](../reference/keys.md) and [Column callables](../reference/column-callables.md): what a lookup and a callable
  take.
- [Entities](entities.md): the entities that an entity column refers to.
- [Relational algebra](relational-algebra.md): sets built over the columns.
- Debug-build checks: [reading an empty `Ref`](../reference/debug-build-checks.md#ref_not_empty),
  [a source holds no element twice](../reference/debug-build-checks.md#source_holds_no_repeat),
  [nor an empty `Ref`](../reference/debug-build-checks.md#source_holds_no_empty_ref).
