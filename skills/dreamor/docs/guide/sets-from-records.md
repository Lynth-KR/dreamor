<!-- Copied from docs/guide/sets-from-records.md (blob 96d2d237f852), tests/docs/guide/model.hpp (blob 906d31015f4d), tests/docs/guide/sets_from_records.cpp (blob 3cea23fbaf59) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Sets from records

Your data often holds a relation as a list of records, one record for each pair that the relation holds. You can make
a set straight from such a list, with one row for each record. A callable that you write turns each record into its
row.

In the [guide's model](index.md), the bill of materials is such a list. A `BomRecord` is one line of it: a unit of
the parent takes `quantity` units of the child. The record names both items by text:

```cpp
struct BomRecord {  // a line of the bill of materials: a unit of parent takes quantity units of child
  std::string parent, child;
  double quantity;
};

```

`p.bom` holds the plant's lines. The frame takes plates and bolts, the plate takes bolts and nuts, and the bracket
takes a plate and bolts:

```cpp
p.bom = {
    {"frame", "plate", 2},
    {"frame", "bolt", 4},
    {"plate", "bolt", 2},
    {"plate", "nut", 2},
    {"bracket", "plate", 1},
    {"bracket", "bolt", 2},
};
```

The examples also use these names of the model:

- `p` is the plant, and `items`, or `p.items`, is its vector of items. [Entities](entities.md) shows it.
- `cParent` is the column for the item that takes another, and `cChild` the column for the item that goes into it.
  `cItem` is the column for an item on its own. [Columns](columns.md) declares them.

## Make a set from records

Your model needs the lines of the bill of materials as a set, with a parent and a child in each row, for example to
write a constraint for each line. Make the set from the records, with a callable that turns a record into a row:

```cpp
const auto edge_of = [&items](const BomRecord& r) {
  return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
};
const dreamor::IndexSet<cParent, cChild> sBOM{bom, edge_of};
```

- `edge_of` takes a record and returns its row. It looks each item up by its name with `items.at`, and returns the
  two `Ref`s as a `std::tuple<cParent, cChild>`. Both columns hold an item, so `edge_of` names the column of each
  `Ref`.
- `dreamor::IndexSet<cParent, cChild> sBOM{bom, edge_of}` calls `edge_of` for each record of `bom` and makes a row of
  each. The set's type names its columns.

`sBOM` has one row for each of the six records, in the order of the records: the frame with the plate first, and the
bracket with the bolt last. Each row holds the parent first, so `sBOM` holds the bracket with the bolt, and not the
bolt with the bracket.

For what `edge_of` may return, and why the set's type names its columns, see
[`IndexSet`](../reference/indexset.md#indexset-constructor).

## Keep a set as a member

A model often keeps its sets in a class, and makes them once, when it makes the class. Suppose you want the lines of
the bill of materials as a member of a class. `Bom` is this page's own class, not part of the model:

```cpp
struct Bom {
  explicit Bom(const Plant& p) : sBOM{edges(p)} {}

  dreamor::IndexSet<cParent, cChild> sBOM;

  static dreamor::IndexSet<cParent, cChild> edges(const Plant& p) {
    const auto edge_of = [&items = p.items](const BomRecord& r) {
      return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
    };
    return dreamor::IndexSet<cParent, cChild>{p.bom, edge_of};
  }
};

```

- `sBOM` is a member of type `IndexSet<cParent, cChild>`.
- The constructor makes `sBOM` in its member initializer list. A set declared with no initializer is empty, and can
  be assigned in the constructor's body instead.
- `edges` is a static member function that makes the set from the plant's records. A member initializer list has no
  room for a local variable, and `edges` gives `edge_of` a place to be named.

The `sBOM` of a `Bom` made from the plant holds the same rows as `sBOM` in the section above, in the same order. Its
`Ref`s refer to the items that `p.items` holds.

For the ways a set is made and assigned, see [`IndexSet`](../reference/indexset.md#indexset).

## Make a set of one column

Suppose that a supplier sells bolts and nuts too, and that your data lists the items that you can buy, by name. You
want those items as a set of one column, `cItem`. `bought` is this page's own data, not part of the model:

```cpp
const std::vector<std::string> bought{"bolt", "nut"};  // items a supplier sells too
const auto item_named = [&items](const std::string& name) { return items.at(name); };
const dreamor::IndexSet<cItem> sBOUGHT{bought, item_named};
```

- `bought` holds the names of the items.
- `item_named` takes a name and returns the `Ref<Item>` to the item. It returns the `Ref` alone, not in a
  `std::tuple`: for a set of one column, a row may be one value.
- `dreamor::IndexSet<cItem> sBOUGHT{bought, item_named}` makes a row of each name. The `Ref` fills `cItem`, the set's
  one column.

`sBOUGHT` holds the bolt and the nut, in the order of `bought`.

For what `item_named` may return, see [`IndexSet`](../reference/indexset.md#indexset-constructor).

## See also

- [`IndexSet` from records](../reference/indexset.md#indexset-constructor): what `row_of` may return, and records
  with a text column.
- [Make a table from records](data-on-a-set.md#make-a-table-from-records): values on the rows of the same records.
- [Relational algebra](relational-algebra.md) and [Set-builder notation](set-builder-notation.md): sets made from
  other sets.
- Debug-build checks: [a source holds no element twice](../reference/debug-build-checks.md#source_holds_no_repeat),
  [nor an empty `Ref`](../reference/debug-build-checks.md#source_holds_no_empty_ref).
