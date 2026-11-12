# Entities

An entity is one thing in your model's world, such as an item or a machine. You hold the entities of one type in an
`EntityVector`, which keeps each entity once and identifies it by its primary key. A `Ref` refers to one entity that
an `EntityVector` holds.

The examples use the [guide's model](index.md): a plant whose machines make items over three periods. Its two entity
types are `Item` and `Machine`:

```cpp
--8<-- "tests/docs/guide/model.hpp:entities"
```

Each type defines `primaryKey()`, which returns what identifies an entity of the type. For an item and for a machine,
the primary key is the name. A machine's `items` holds a `Ref` to each item that the machine makes.

## Hold each entity once

Suppose that your data lists the same item twice. Your model needs each item once, so that the item's data comes
from one place. Insert the items into an `EntityVector`, which keeps the first item with a given key. This example
fills a vector of its own, apart from the model's items:

```cpp
--8<-- "tests/docs/guide/entities.cpp:hold-once"
```

- The first two `insert` calls add a washer and a gear.
- The third `insert` tries to add a second gear. A gear is held already, so `insert` returns `false` and leaves the
  first gear as it was.
- `sort()` puts the items in the order of their primary keys, so the gear comes before the washer.
- `freeze()` ends the filling. After `freeze()`, you read the vector: you look its items up, iterate over them and
  take `Ref`s to them.

`items` holds the gear and the washer, in that order, because "gear" comes before "washer". The gear is the one
inserted first, with its time of 0.2 hours: the second gear changed nothing.

For what each phase of the vector allows, see [`EntityVector`](../reference/entities.md#entityvector),
[`insert`](../reference/entities.md#entityvector-insert), [`sort`](../reference/entities.md#entityvector-sort) and
[`freeze`](../reference/entities.md#entityvector-freeze).

## Look an entity up by its key

Your data often names an entity by its key, such as the item name `frame`. To get the entity that a key identifies,
look the key up in the frozen vector. Here `items` is the model's vector of items: bolt, bracket, frame, nut and
plate.

```cpp
--8<-- "tests/docs/guide/entities.cpp:look-up"
```

- `items.at("frame")` returns a `Ref<Item>` to the frame. The `Ref` refers to the frame that `items` holds, not to a
  copy of it. If no item has the key, `at` throws `std::out_of_range`.
- `items.find(wanted)` also returns a `Ref`. If no item has the key, the `Ref` is empty: it refers to no item.
- `if (!gear)` reads `gear` as a condition, in which an empty `Ref` is false. No item is named gear, so the `if`
  logs a warning.

`frame` refers to the model's frame, and `gear` is empty. The log holds one warning line, `no item named gear`.

Use `at` if a missing key is an error in your data, and `find` if your model handles a missing key. For the keys
that each function takes, see [`at`](../reference/entities.md#entityvector-at), [`find`](../reference/entities.md#entityvector-find) and
[`Ref`](../reference/entities.md#ref).

## Take the `Ref` of an entity at hand

A loop over a vector gives you each entity itself, while a column of entities, the key of a variable family and a sum
take a `Ref`. To turn the entity in your hand into its `Ref`, ask the vector for it rather than look its key up again.
Here `items` is the model's vector of items, in key order: bolt, bracket, frame, nut and plate.

```cpp
--8<-- "tests/docs/guide/entities.cpp:at-hand"
```

- `items.ref(item)` returns the `Ref` to `item`, which must be one of the elements of `items`, as the loop gives it. It
  finds the item by its place in the vector, so it looks no key up.
- `items.refs()` gives every item as a `Ref`, in the vector's order.

`slow` holds the bracket, the frame and the plate, the items that take half an hour or more, and `every` holds all
five items. A copy of an item is not an element of `items`, and a debug build stops if `ref` receives one.

For both functions, see [`ref`](../reference/entities.md#entityvector-ref) and
[`refs`](../reference/entities.md#entityvector-refs).

## Refer to another entity

A machine makes several items, and your model reads a machine's items together with the machine. Give each machine
a `Ref` to each item that it makes, not a copy of the item and not the item's name. In the model, `p` is the plant,
and `p.items` and `p.machines` are its vectors:

```cpp
--8<-- "tests/docs/guide/model.hpp:items-data"
```

```cpp
--8<-- "tests/docs/guide/model.hpp:machines-data"
```

- The `insert` calls add the items, and `p.items.sort().freeze()` sorts the items and freezes the vector.
- Each `Machine{...}` takes the `Ref`s that `p.items.at` returns for the items that the machine makes. The items are
  frozen first, because the machines look them up.
- `p.machines.sort().freeze()` sorts the machines and freezes their vector.

M1 makes the frame, the plate and the bracket. M2 makes the plate, the bolt and the nut. Each `Ref` in a machine's
`items` refers to the item that `p.items` holds, not to a copy of it.

For what a `Ref` compares and how long it stays valid, see [`Ref`](../reference/entities.md#ref).

## Read an entity through a `Ref`

A condition or a sum often has a `Ref` to a machine and needs the machine's data, such as its hours in a period.
Read the data through the `Ref`. Here `machines` is the model's vector of machines, M1 and M2:

```cpp
--8<-- "tests/docs/guide/entities.cpp:read"
```

- `m1->capacity` reads a member of M1, its hours in a period.
- `*m1` returns M1 itself, the machine that `machines` holds.
- `std::format` writes a `Ref` as its entity's primary key. `m1` is written as `M1`, and the first `Ref` in M1's
  `items` is written as `frame`.

`hours` is 4. `machine` is the machine that `machines` holds, not a copy of it. `line` is
`M1: 4 hours, frame first`.

For the operators, see [`operator->`](../reference/entities.md#ref-operator-arrow),
[`operator*`](../reference/entities.md#ref-operator-star) and [`Ref`](../reference/entities.md#ref).

## Give an entity a composite key

Suppose that each machine has fixtures, and that fixtures are named per machine: M1 and M2 can each have a fixture
F1. The name alone does not identify a fixture, but the machine and the name together do. Return both from
`primaryKey()` as a `std::tuple`. `Fixture` is this page's own type, not part of the model:

```cpp
--8<-- "tests/docs/guide/entities.cpp:fixture"
```

Fill a vector of fixtures, and look one up by its whole key:

```cpp
--8<-- "tests/docs/guide/entities.cpp:composite-key"
```

- The two `insert` calls add M1's F1 and M2's F1. The two keys differ in their machine, so the vector keeps both
  fixtures.
- `fixtures.at` takes the whole key in braces: the `Ref` to M2 and the name `F1`.
- `std::format` writes the `Ref` to the fixture as the parts of its key, joined by `_`.

`fixtures` holds both fixtures. `fixture` refers to M2's F1, and `label` is `M2_F1`.

To join the parts of a key with another separator, define `primaryKeySeparator()`, as the introduction of
[Entities](../reference/entities.md) describes.

## See also

- [Entities](../reference/entities.md): every member of `EntityVector` and `Ref`.
- Debug-build checks: [filling an `EntityVector`](../reference/debug-build-checks.md#entity_vector_filling),
  [reading an `EntityVector`](../reference/debug-build-checks.md#entity_vector_reading),
  [reading an empty `Ref`](../reference/debug-build-checks.md#ref_not_empty).
