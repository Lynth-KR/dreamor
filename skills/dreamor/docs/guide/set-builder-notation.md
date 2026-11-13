<!-- Copied from docs/guide/set-builder-notation.md (blob 45441e64cdbe), tests/docs/guide/set_builder_notation.cpp (blob 919b22a43b4c), tests/docs/guide/model.hpp (blob 906d31015f4d) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Set-builder notation

Set-builder notation writes a set as mathematics does: the columns of its rows, then where the values of each column
come from. In dreamor, `of` names the set's columns, which are the set's head. Each `in` is a generator: it binds one
or more columns, which means that it adds the columns to the rows, with values from a source. A source is what a
generator reads values from, such as an `EntityVector`, an interval or a set. `build()` then makes the rows and returns
them as an `IndexSet`.

`of` lowers the generators onto a chain: it turns each generator into a step of the chains that
[Relational algebra](relational-algebra.md) shows. So the two notations build the same sets, and which of them you
write is your choice.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `p` is the plant. `p.machines` holds the machines M1 and M2. M1 makes the frame, the plate and the bracket. M2
  makes the plate, the bolt and the nut. [Entities](entities.md) shows them.
- `p.periods` is the number of periods, 3.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. `cParent` and `cChild` are
  the columns for the item that takes another and the item that goes into it. [Columns](columns.md) declares them.

## Write a set in set-builder notation

Suppose you want each machine with each item that it makes, in each period in which customers buy the item. In
mathematics, the set is `{(m, i, t) | m ∈ machines, i ∈ items(m), t ∈ {1, 2, 3}, demand(i, t) > 0}`. Write the same
set with `of` and `in`:

```cpp
using namespace dreamor::index_set::set_builder;

const auto items_of  = [](cMachine m) -> const auto& { return m->items; };
const auto periods   = dreamor::interval::closed(1, p.periods);
const auto in_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };

const auto sSERVE = of<cMachine, cItem, cPeriod>(in<cMachine>(p.machines), in<cItem>(items_of), in<cPeriod>(periods))
                        .where(in_demand)
                        .build();
```

- The `using` directive makes `of` and `in` available without their namespace, `dreamor::index_set::set_builder`.
- `items_of` returns the items that a machine makes.
- `periods` is the integers from 1 to 3, both included.
- `in_demand` is true if customers buy the item in the period.
- `of<cMachine, cItem, cPeriod>` names the set's columns, in their order.
- `in<cMachine>(p.machines)` binds `cMachine`, with a row for each machine.
- `in<cItem>(items_of)` binds `cItem`, with a row for each item that the row's machine makes. `items_of` reads
  `cMachine`, which the generator before it binds.
- `in<cPeriod>(periods)` binds `cPeriod`, and pairs every row with every period.
- `where(in_demand)` keeps only the rows for which `in_demand` is true, and `build()` makes the rows.

Customers buy the frame in each period, and the nut in period 2. M1 makes the frame, and M2 makes the nut. So
`sSERVE` holds M1 with the frame in periods 1, 2 and 3, and M2 with the nut in period 2: 4 rows. The chain
`from<cMachine>(p.machines).lateral<cItem>(items_of).cross<cPeriod>(periods).where(in_demand)` builds the same rows.

For what each part takes, see [`of`](../reference/set-builder-notation.md#of),
[`in`](../reference/set-builder-notation.md#in), [`where`](../reference/set-builder-notation.md#setof-where),
[`build`](../reference/set-builder-notation.md#setof-build) and [`closed`](../reference/intervals.md#closed).

## Bind a set's columns with one generator

Suppose you have each machine with each item that it makes as a set, `sROUTES`, and you want each of those pairs in
each period. The rows of a set hold their columns together, so bind the columns together, with one generator over the
set:

```cpp
const auto sROUTES        = of<cMachine, cItem>(in<cMachine>(p.machines), in<cItem>(items_of)).build();
const auto sROUTE_PERIODS = of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(sROUTES), in<cPeriod>(periods)).build();
```

- The first line builds `sROUTES` from the machines and the items of each machine. `items_of`, from the section above,
  returns a machine's items.
- `in<cMachine, cItem>(sROUTES)` binds `cMachine` and `cItem` at once: each row of `sROUTES` gives a machine and an
  item together.
- `in<cPeriod>(periods)` pairs every row with every period.

M1 and M2 make 3 items each, so `sROUTES` holds 6 rows. `sROUTE_PERIODS` holds each of those rows in each of the 3
periods: 18 rows.

For what a generator over a set must name, see [`in`](../reference/set-builder-notation.md#in).

## Keep the rows a bound column allows

Suppose you want each line of the bill of materials with each machine that makes the line's parent, because that
machine uses the child when it makes the parent. `sBOM` holds the lines of the bill of materials, each a parent and a
child. [Sets from records](sets-from-records.md) makes it. To keep only the machines that make the parent, add a
generator over `cParent`, which `sBOM` binds already. A generator over a column that an earlier generator binds adds
no column. It is a condition: it keeps the rows whose value in the column is among the values of its source.

```cpp
const auto sCONSUME =
    of<cParent, cChild, cMachine>(in<cParent, cChild>(sBOM), in<cMachine>(p.machines), in<cParent>(items_of)).build();
```

- `in<cParent, cChild>(sBOM)` binds `cParent` and `cChild` from the lines of `sBOM`.
- `in<cMachine>(p.machines)` pairs every line with every machine.
- `in<cParent>(items_of)` names `cParent`, which `sBOM` binds already, so the generator is a condition. It keeps a row
  if the row's parent is one of the items that `items_of` returns for the row's machine.

Only M1 makes the frame and the bracket, so the two lines of the frame and the two lines of the bracket each come
with M1 alone. The plate is made on M1 and on M2, so its two lines each come with both machines. `sCONSUME` holds 8
rows, and none of them pairs a line of the frame with M2.

For what a generator does over a column that is bound already, see [`in`](../reference/set-builder-notation.md#in).

## Write the generators in any order

You can write the generators in the order in which you think of them. The head, not the generators, decides the
columns of the set and their order. Here the generators of `sSERVE`, from the first section, come in another order,
with the periods first:

```cpp
const auto sSERVE_any_order =
    of<cMachine, cItem, cPeriod>(in<cPeriod>(periods), in<cMachine>(p.machines), in<cItem>(items_of))
        .where(in_demand)
        .build();
```

`in<cPeriod>(periods)` now comes first, and the head is the same, `of<cMachine, cItem, cPeriod>`. `in<cItem>(items_of)`
still comes after `in<cMachine>(p.machines)`, because a generator whose callable reads a column comes after the
generator that binds the column.

`sSERVE_any_order` holds the same rows as `sSERVE`, with its columns in the order of the head. `build()` chooses the order in
which it reads the sources, whatever the order in which you write the generators: both sets read the periods, then
the machines, then each machine's items. So even the rows of `sSERVE_any_order` come in the order of the rows of `sSERVE`.

For the rules of the head and the generators, see [`of`](../reference/set-builder-notation.md#of) and
[`in`](../reference/set-builder-notation.md#in). For the order in which `build()` reads the sources, see
[`visitOrder`](../reference/set-builder-notation.md#setof-visitorder).

## Write the model's sets in either notation

Suppose you have a set written as a chain, and you want it in set-builder notation, or the reverse. The guide's model
writes its sets both ways, to the same rows. Read the two side by side to see which generator each step becomes. The
model's sets use these names:

- `quantity` is the bill of materials as a table. `quantity.keys()` is its lines, a set of `cParent` and `cChild`.
- `is_sold` is true for an item that customers buy in some period, and `as_item` returns a part, a `cChild`, as a
  `cItem`.
- `as_parent` takes a row of `sPRODUCE` and returns the row with its item as a `cParent`.

The sets as chains, first `sNEEDED`, the items that the plant needs, then the sets that read it:

```cpp
const auto sSOLD   = from<cItem>(p.items).where(is_sold).build();
const auto sBELOW  = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
const auto sNEEDED = dreamor::unite(sSOLD, from<cParent>(sSOLD).join(sBELOW).build().select(as_item));
```

```cpp
const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, p.periods)).build();
const auto sPRODUCE = from<cMachine>(p.machines).lateral<cItem>(items_of).join(sNEEDED).cross(sPERIODS).build();
const auto sSTOCK   = from(sNEEDED).cross(sPERIODS).build();
const auto sCONSUME = from(quantity.keys()).join(sPRODUCE.select(as_parent)).build();
```

The same sets in set-builder notation:

```cpp
const auto sSOLD   = of<cItem>(in<cItem>(p.items)).where(is_sold).build();
const auto sBELOW  = dreamor::transitiveClosure(quantity.keys());
const auto sPARTS  = of<cParent, cChild>(in<cParent, cChild>(sBELOW), in<cParent>(sSOLD)).build().select(as_item);
const auto sNEEDED = dreamor::unite(sSOLD, sPARTS);
```

```cpp
const auto sPERIODS = of<cPeriod>(in<cPeriod>(periods)).build();
const auto sPRODUCE = of<cMachine, cItem, cPeriod>(
                          in<cMachine>(p.machines), in<cItem>(items_of), in<cItem>(sNEEDED), in<cPeriod>(periods))
                          .build();
const auto sSTOCK   = of<cItem, cPeriod>(in<cItem>(sNEEDED), in<cPeriod>(periods)).build();
const auto sCONSUME =
    of<cParent, cChild, cMachine, cPeriod>(in<cParent, cChild>(quantity.keys()),
                                           in<cMachine>(p.machines),
                                           in<cParent>(items_of),  // a condition: m makes the parent
                                           in<cParent>(sNEEDED),   // and the parent is needed
                                           in<cPeriod>(periods))
        .build();
```

- For `sSOLD`, `from<cItem>(p.items).where(is_sold)` becomes `of<cItem>(in<cItem>(p.items)).where(is_sold)`.
- For the parts below a sold item, the chain starts from `sSOLD` and joins the closure `sBELOW` on `cParent`. The
  set-builder notation starts from `sBELOW`, which binds `cParent` and `cChild`, and adds `in<cParent>(sSOLD)`, a
  condition that keeps the pairs whose parent customers buy. Both select the parts as `cItem`, and `unite` adds them
  to `sSOLD`.
- For `sPERIODS`, `from<cPeriod>` over the interval becomes `in<cPeriod>(periods)`.
- For `sPRODUCE`, `from<cMachine>(p.machines)` becomes `in<cMachine>(p.machines)`, and `lateral<cItem>(items_of)`
  becomes `in<cItem>(items_of)`. `join(sNEEDED)` becomes `in<cItem>(sNEEDED)`, a condition that keeps the needed
  items, because `cItem` is bound already. `cross(sPERIODS)` becomes `in<cPeriod>(periods)`.
- For `sSTOCK`, `from(sNEEDED)` and `cross(sPERIODS)` become a generator each.
- For `sCONSUME`, the chain matches each line with the rows of `sPRODUCE` whose item is the line's parent. The
  set-builder notation pairs each line with each machine and each period, and keeps the rows by two conditions.
  `in<cParent>(items_of)` keeps a row if the machine makes the parent. `in<cParent>(sNEEDED)` keeps a row if the parent
  is needed, and reads the items of `sNEEDED` as parents.

Each set in one notation holds the same rows as the set of the same name in the other. The plant does not need the
bracket, so `sPRODUCE` holds M1 with the frame and the plate, and M2 with the plate, the bolt and the nut, each in the
three periods: 15 rows. `sROUTE_PERIODS`, from
[Bind a set's columns with one generator](#bind-a-sets-columns-with-one-generator), holds the bracket too. `sCONSUME`
holds the lines of the frame with M1, and the lines of the plate with M1 and with M2, each in the three periods. It
holds no line of the bracket.

For each step of a chain, see [Relational algebra](../reference/relational-algebra.md). For each generator, see
[`in`](../reference/set-builder-notation.md#in).

## See also

- [Set-builder notation](../reference/set-builder-notation.md): `of`, `in`, and the set that they describe until
  `build()`.
- [Relational algebra](relational-algebra.md): the same sets as chains.
- [Sets from records](sets-from-records.md): a set made from the records that hold a relation.
- Debug-build checks: [a source holds no element twice](../reference/debug-build-checks.md#source_holds_no_repeat),
  [nor an empty `Ref`](../reference/debug-build-checks.md#source_holds_no_empty_ref).
