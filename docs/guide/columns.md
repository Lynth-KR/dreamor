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
--8<-- "tests/docs/guide/model.hpp:columns"
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
--8<-- "tests/docs/guide/columns.cpp:read"
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
--8<-- "tests/docs/guide/columns.cpp:two-columns"
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
--8<-- "tests/docs/guide/columns.cpp:callable"
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
--8<-- "tests/docs/guide/columns.cpp:key"
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
--8<-- "tests/docs/guide/columns.cpp:named-key"
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
--8<-- "tests/docs/guide/columns.cpp:enum-text-columns"
```

```cpp
--8<-- "tests/docs/guide/columns.cpp:enum-text"
```

- `DREAMOR_VALUE_COLUMN(cShift, Shift)` declares a column that holds a `Shift`, an enum.
- `DREAMOR_VALUE_COLUMN(cSupplier, std::string)` declares a column that holds a `std::string`.
- `shift == Shift::kNight` compares the value of `shift` with an enumerator.
- `supplier == "Acme"` compares the text of `supplier` with a string literal, character by character.

`atNight` and `fromAcme` are true.

For every type that a value column may hold, see [`Col`](../reference/columns.md#col).

## Declare a column of a type of your own

Suppose your model has a step for each distinct number of hours that a unit of an item takes, such as the steps of a
staircase cost, and links each item to the step of its hours. The members of the set of steps are measured values.
A column of `double` does not compile, so that a quantity goes into a table on a set or into an entity. To make
measured values the members of a set, hold each value in a type of your own, with `==` and `std::hash`. `Hours` and
`cHours` are this page's own, not part of the model:

```cpp
--8<-- "tests/docs/guide/columns.cpp:own-type"

--8<-- "tests/docs/guide/columns.cpp:own-type-hash"

--8<-- "tests/docs/guide/columns.cpp:own-type-column"
```

```cpp
--8<-- "tests/docs/guide/columns.cpp:own-type-sets"
```

- `Hours` holds the hours that a unit takes. Its `==` compares the hours exactly, and its `std::hash` hashes them,
  so the column finds a row by its hours.
- `std::formatter<Hours>` writes the hours in a name, as `std::format` writes a `double`.
- `DREAMOR_VALUE_COLUMN(cHours, Hours)` declares a column that holds an `Hours`. The specialization of `std::hash`
  comes before it.
- `select(with_hours)` makes a set of each item with its hours, in a new column `cHours`.
- `select<cHours>()` keeps the column `cHours` alone, and drops each row that repeats an earlier one.
  `orderBy(by_hours, dreamor::descending)` puts the largest hours first.
- `groupBy<cHours>()` groups the items by their hours, and `slice(Hours{0.1})` returns the items that take 0.1 hours.
- `indexedName("Step", ...)` writes the name of the first step, as a `VarTable` names a variable.

The frame takes 1 hour, the plate and the bracket take 0.5 hours each, and the bolt and the nut take 0.1 hours each.
So `sHOURS_OF` holds 5 rows, and `sSTEPS` holds the three distinct values, 1, 0.5 and 0.1, in that order. `quick` is
2, for the bolt and the nut, and `top` is `Step_1`.

A row is found by the `==` and the hash of your type, so the values that you mean as one member must be equal to the
last bit. Hours that your data gives are, but hours computed in two ways, such as `0.1 + 0.2` and `0.3`, may differ
in the last bit and make two rows.

A `std::optional` of such a value has `==` and `std::hash` too, so it can be a column for a key part that some rows
have and others lack. [Pick the first row of each group](grouping.md#pick-the-first-row-of-each-group) shows one.

For the rules of a type of your own in a column, see [`Col`](../reference/columns.md#col). For how a value is
written in a name, see [Names](../reference/names.md).

## See also

- [Columns](../reference/columns.md): every member of a column.
- [Keys](../reference/keys.md) and [Column callables](../reference/column-callables.md): what a lookup and a callable
  take.
- [Entities](entities.md): the entities that an entity column refers to.
- [Relational algebra](relational-algebra.md): sets built over the columns.
- Debug-build checks: [reading an empty `Ref`](../reference/debug-build-checks.md#ref_not_empty),
  [a source holds no element twice](../reference/debug-build-checks.md#source_holds_no_repeat),
  [nor an empty `Ref`](../reference/debug-build-checks.md#source_holds_no_empty_ref).
