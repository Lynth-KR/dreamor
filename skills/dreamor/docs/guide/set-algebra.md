<!-- Copied from docs/guide/set-algebra.md (blob 4c354edc25ee), tests/docs/guide/set_algebra.cpp (blob 860e834d0d55), tests/docs/guide/model.hpp (blob b2d1db7441b4) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Set algebra

Set algebra combines two sets of the same columns into a new set, or compares them: the union, the intersection and
the difference, equality and inclusion. A relation from an item to an item, such as the bill of materials, also has a
transitive closure: every pair of items that a path of the relation leads to. Each operation reads its sets and
leaves them as they were. To derive a set from one set, see [Deriving sets](deriving-sets.md).

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket. [Entities](entities.md) shows
  the items.
- `cItem` is the column for an item. `cParent` and `cChild` each hold an item too: in a line of the bill of
  materials, the parent takes the child. [Columns](columns.md) declares the columns.
- `quantity` is the bill of materials, a `ParamTable<double, cParent, cChild>`, and its `keys()` are its lines. The
  frame takes plates and bolts, the plate takes bolts and nuts, and the bracket takes plates and bolts.
  [Make a table from records](data-on-a-set.md#make-a-table-from-records) makes it.

The first two sections combine and compare two sets of items: the items that customers buy, and the items that a line
of the bill of materials takes.

```cpp
using dreamor::index_set::relational::from;

const auto is_sold = [](cItem i) { return std::ranges::max(i->demand) > 0; };
const auto as_item = [](cChild c) { return cItem{c}; };

const auto sSOLD  = from<cItem>(items).where(is_sold).build();  // what customers buy
const auto sPARTS = quantity.keys().select(as_item);            // what another item takes
```

- `is_sold` is true for an item whose demand is above 0 in some period. The chain of `sSOLD` keeps the items for
  which `is_sold` is true.
- `as_item` returns the child of a line as a `cItem`. `quantity.keys().select(as_item)` makes the child of each line
  a row of `sPARTS`, and holds each item once.

`sSOLD` holds the frame and the nut. `sPARTS` holds the plate, the bolt and the nut, in the order in which the lines
first take them.

## Combine two sets

Suppose you want the items that customers buy or that another item takes, the items that are both, and the items
that customers buy and no other item takes. To combine two sets of the same columns, call `unite`, `intersect` or
`difference`:

```cpp
const auto sSOLD_OR_PART = dreamor::unite(sSOLD, sPARTS);       // frame, nut, plate, bolt
const auto sSOLD_PART    = dreamor::intersect(sSOLD, sPARTS);   // nut
const auto sEND_PRODUCTS = dreamor::difference(sSOLD, sPARTS);  // frame: sold, and a part of no item
```

- `dreamor::unite(sSOLD, sPARTS)` returns the rows that are in either set: the rows of `sSOLD` in their order, then
  the rows of `sPARTS` that `sSOLD` does not have.
- `dreamor::intersect(sSOLD, sPARTS)` returns the rows of `sSOLD` that are also in `sPARTS`.
- `dreamor::difference(sSOLD, sPARTS)` returns the rows of `sSOLD` that are not in `sPARTS`.

Customers buy the nut, and the plate takes nuts, so the nut is in both sets. `sSOLD_OR_PART` holds the frame and the
nut, then the plate and the bolt, and it holds the nut once. The bracket is in neither set, so `sSOLD_OR_PART` does
not hold it. `sSOLD_PART` holds the nut alone. `sEND_PRODUCTS` holds the frame, which customers buy and no line
takes.

For what each operation takes, see [`unite`](../reference/set-algebra.md#unite),
[`intersect`](../reference/set-algebra.md#intersect) and [`difference`](../reference/set-algebra.md#difference).

## Compare two sets

Suppose you built a set in two ways and want to know whether the two sets hold the same rows. Suppose you also want to
know whether every item that customers buy is a part of another item. To compare two sets, use `==` or `within`:

```cpp
const auto sPART_OR_SOLD = dreamor::unite(sPARTS, sSOLD);  // plate, bolt, nut, frame

const bool same_rows  = sPART_OR_SOLD == sSOLD_OR_PART;                    // true
const bool same_order = std::ranges::equal(sPART_OR_SOLD, sSOLD_OR_PART);  // false
const bool all_parts  = dreamor::within(sSOLD, sPARTS);                    // false: the frame is no part
const bool part_too   = dreamor::within(sSOLD_PART, sPARTS);               // true
```

- `dreamor::unite(sPARTS, sSOLD)` is the union of the section above with its operands swapped, so the rows of
  `sPARTS` come first.
- `==` returns true if the two sets hold the same rows, in any order.
- `std::ranges::equal` compares the rows in their order, as it does for any two ranges.
- `dreamor::within(sSOLD, sPARTS)` returns true if every row of `sSOLD` is in `sPARTS`.

`sPART_OR_SOLD` holds the plate, the bolt, the nut and the frame. `sSOLD_OR_PART` holds the same four items, in
another order. So `same_rows` is true, and `same_order` is false. The frame is not a part of another item, so
`all_parts` is false. `sSOLD_PART` holds the nut alone, which `sPARTS` holds, so `part_too` is true.

For what each comparison takes, see [`operator==`](../reference/set-algebra.md#operator-eq) and
[`within`](../reference/set-algebra.md#within).

## Find every part below an item

An item needs every part that goes into it, directly or through other parts. The frame takes plates, and a plate
takes nuts, so the frame needs nuts too, although no line of the bill of materials has the frame take a nut. To find
every part below each item, call `transitiveClosure` with the lines:

```cpp
const auto sBELOW    = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
const bool lines_too = dreamor::within(quantity.keys(), sBELOW);     // true: a line is a path of one edge
```

- `dreamor::transitiveClosure(quantity.keys())` returns every pair of an item and a part that a path of one or more
  lines leads to. A line is a path of one line, and a part that an item takes through the plate is at the end of a
  path of two lines. Each pair comes once, however many paths lead to it.
- `dreamor::within(quantity.keys(), sBELOW)` returns true if every line is in `sBELOW`.

`sBELOW` is an `IndexSet<cParent, cChild>`. It holds the six lines, so `lines_too` is true. It also holds the nut below
the frame and the nut below the bracket, each through the plate: 8 rows. The frame takes bolts directly and also
through the plate, and `sBELOW` holds that pair once.

For what `transitiveClosure` takes and the order of its rows, see
[`transitiveClosure`](../reference/set-algebra.md#transitiveclosure).

## Combine sets whose columns come in another order

Two sets may have the same columns in another order, such as the lines of the bill of materials listed with the part
first. Suppose you want the pairs of `sBELOW` that no line holds: the parts that an item takes only through another
part. Set algebra matches the rows of two sets by the columns' names, so the order of the columns does not matter:

```cpp
const auto sUSED_IN   = quantity.keys().select<cChild, cParent>();  // each line, its part first
const bool same_lines = sUSED_IN == quantity.keys();                // true
const auto sINDIRECT  = dreamor::difference(sBELOW, sUSED_IN);      // the pairs no line joins
```

- `quantity.keys().select<cChild, cParent>()` returns the lines with the column `cChild` first.
- `==` matches the rows of `sUSED_IN` with the rows of `quantity.keys()` by the columns' names.
- `dreamor::difference(sBELOW, sUSED_IN)` returns the pairs of `sBELOW` that `sUSED_IN` does not hold. The new set
  has the columns of the left operand, `sBELOW`, in their order.

`sUSED_IN` holds the same lines as `quantity.keys()`, so `same_lines` is true. `sINDIRECT` is an
`IndexSet<cParent, cChild>`. It holds the frame with the nut and the bracket with the nut, the two pairs that only a
path through the plate leads to.

For how the rows of the right operand are matched, see the introduction of
[Set algebra](../reference/set-algebra.md) and [`difference`](../reference/set-algebra.md#difference).

## Find the items the products need

The plant needs the items that customers buy, and every part below them. The model builds this set, `sNEEDED`, from
the closure and a union:

```cpp
const auto sSOLD   = from<cItem>(p.items).where(is_sold).build();
const auto sBELOW  = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
const auto sNEEDED = dreamor::unite(sSOLD, from<cParent>(sSOLD).join(sBELOW).build().select(as_item));
```

`is_sold` and `as_item` are the callables from the top of this page, and `p.items` is the model's `items`.

- `sSOLD` holds the items that customers buy, and `sBELOW` is the closure from
  [Find every part below an item](#find-every-part-below-an-item).
- `from<cParent>(sSOLD).join(sBELOW).build()` pairs each sold item with each part below it.
  [Fill one column from a set of another](relational-algebra.md#fill-one-column-from-a-set-of-another) builds these
  pairs step by step.
- `select(as_item)` makes each part a row of `cItem`.
- `dreamor::unite` returns the sold items, then the parts that are not sold.

`sNEEDED` holds the frame and the nut, which customers buy, then the plate and the bolt, which the frame takes. The nut
is also below the frame, and `sNEEDED` holds it once. The bracket is not in `sNEEDED`, because no customer buys it and
no sold item takes it.

For what the join takes, see [`join`](../reference/relational-algebra.md#chain-join).

## Include each item itself

The closure pairs an item with the parts below it, but not with itself. Suppose you want each item with itself and
with every part below it, as for a cost that adds up over an item and all its parts. To include each item itself,
unite the closure with a set that pairs each item with itself:

```cpp
const auto as_self = [](cItem i) { return std::tuple<cParent, cChild>{i, i}; };  // an item and itself

const auto sSELF        = from<cItem>(items).build().select(as_self);
const auto sAT_OR_BELOW = dreamor::unite(sBELOW, sSELF);  // an item, and every item below it
```

- `as_self` returns the item twice, as a `cParent` and as a `cChild`.
- `from<cItem>(items).build()` makes a set of every item, and `select(as_self)` makes the row of each item the pair of
  the item with itself.
- `dreamor::unite(sBELOW, sSELF)` returns the pairs that are in either set.

The bill of materials has no cycle, so no item is below itself, and `sBELOW` holds no pair of an item with itself.
`sSELF` holds a pair for each of the five items. `sAT_OR_BELOW` holds the 8 pairs of the closure and these 5: 13
rows. It pairs the frame with the frame, the plate, the bolt and the nut, and the nut with the nut alone.

For what the closure holds where the relation has a cycle, see
[`transitiveClosure`](../reference/set-algebra.md#transitiveclosure).

## See also

- [Set algebra](../reference/set-algebra.md): every operation on two sets, and the transitive closure.
- [Deriving sets](deriving-sets.md): a set derived from one set, by `where`, `select`, `orderBy` or membership.
- [Relational algebra](relational-algebra.md): a set built step by step by a chain, with the closure as a source.
- [Sets from records](sets-from-records.md): the bill of materials' lines, made as a set from its records.
