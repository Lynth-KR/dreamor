# Differential tests

For people working on dreamor; `AGENTS.md` says when to read it. Why the random tests are what they are, why that is
enough, and how to check that it still is after a change. What each test runs and in which configuration is
`testing.md`'s.

## What they check

A set has a meaning that needs no index: nested loops over its sources in the order they are written, every condition
applied to the full row. Each operation on a set has one too: what a few lines of plain STL compute. A differential
test computes the same result with dreamor and with that reference, over random data, and compares. It checks that
the implementation keeps the meaning; it does not check the meaning.

| Test | What it varies | Reference |
|---|---|---|
| `tests/stress/test_differential*.cpp` (`differential.hpp`) | one chain over many data, nine row layouts, about twenty-five operations on the result | nested loops, `std::set`, `std::map` |
| `tests/stress/test_shapes.cpp` | the set engine's situations, one shape each, as a chain and in set-builder notation | nested loops |
| `tests/stress/test_row_hints.cpp` | a lookup by columns against hints that point anywhere: every byte of the rows and just outside them, keys of 4 to 80 bytes, sets gone and made again, eight threads | the lookup by the row itself, which its address answers |
| `tests/stress/test_closure_differential.cpp` | transitive closure over random relations | a walk over the edges |
| the differential tests' large cases, in the release presets | sets past 16384 rows | as above |
| `bench/catalog`, by hand | 1e5 to 1e6 rows and more, in a release build | a hand-written arm; each phase's check must agree |

## Where the code branches

A branch of the set layer is chosen by one of three things: the shape of a set (at compile time), the value types of
its columns (at compile time), or the data (at run time).

### The engine: chosen by the shape

`internal/index_set/set.hpp` (`generate`, `visitSpread`, `spread`), the chain's steps (`relational.hpp`) and the
lowering of set-builder notation (`set_builder.hpp`) choose their code with `if constexpr`, from the kinds of the
sources, which of their columns are bound, and where the conditions run. The data only decides how often a loop runs,
whether a condition holds, and whether a join finds its key. The situations, by the letters
`tests/stress/test_shapes.cpp` uses:

- **A**, a source of one column, the same range for every row: A1 a named container, A2 an interval, A3 values in
  braces, A4 a set of one column of another name (read through `FirstColumn`), A5 an `EntityVector`.
- **B**, a source of one column that a callable fills per row: B1 a span (its repeat check runs once per range), B2 an
  interval, B3 a set of one column, B4 a container returned by value (kept for the loop, checked each time).
- **C**, a set or a range of rows, each as an `IndexSet` (the join index shares its rows) and as a `std::vector` of
  tuples (the join index copies them): C1 none of its columns bound (scanned), C2 some bound - one, or two in another
  order than the set's - and some new (joined through `Grouped`), C3 all bound (a semijoin).
- **D**, where a condition runs: D1 on a column a one-column source binds, D2 on a column a set binds, D3 two at one
  column, D4 none in the whole set (`build()` reserves the rows, `exactRows`).
- **E**, a set-builder generator over a column bound already: E1 a range of values (`join(rowsOfValues)`), E2 a set of
  one column of another name, E3 a callable returning values (`AmongListOf`), E4 one returning an interval, E5 one
  returning a set of one column, E6 a temporary range of values (`StatementView`).

### Value types: where a value fills or matches a column

The engine's code is the same for every value type except in two places. `toColumn` (`column.hpp`) chooses by the
element's type and the column's: the same type, a `Ref`, a column holding a `Ref`, an entity of a frozen
`EntityVector`, an interval's value of another integer type, an integer of another type, text into a `std::string`
column, another convertible value. `AmongListOf` compares a bound column with what a callable returns: a value of
another integer type goes through `std::cmp_equal`. These are situation **F**: F1 the same type, F1r a `Ref`, F2 an
integer of another type, F3 an interval of another integer type, F4 a column holding a `Ref`, F5 an entity of a frozen
`EntityVector`, F6 text into a `std::string` column, F7 string literals into a `std::string_view` column, F8 an integer
of another type matched by a callable generator.

What else differs by type is the hash of an element (`hashElement`: an integer's or an enum's bits, a pointer's or a
`Ref`'s address, `std::hash` for the rest), equality, and the ownership of a `std::string` in the rows. `TupleHash` is
one fold over a row's elements, the same at every position, so no branch depends on how types are combined in a row.
The differential tests' nine layouts hold each kind: integers of mixed width and sign, an enum, `Ref`, `std::string_view`,
`std::string`, a user type whose hash collides.

### Data: the index layer alone

Only the indexes choose code by the data:

| Code | Branch | Reached when |
|---|---|---|
| `FlatIndex` (`internal/base/flat_index.hpp`) | untagged slots / tagged slots (`kTaggedFrom`) | more than 16384 entries (2^16 slots at a load of at most 1/2) |
| `FlatIndex::grow` | doubling, and the step from 2^15 to 2^16 slots, where the tags begin | entries inserted one by one past 16384 |
| `hashedBlocks` | blocks of `kHashBlock` (32) keys, the last one partial | any count |
| `FlatIndex::seek` | a probe that wraps at the table's end; equal tags with different keys | the hashes; the colliding layout |
| `Grouped` (`grouped.hpp`) | rows of a key in runs / a key that comes back (a counting sort) | the rows' order against the key |
| `RowIndex::built` (`backend/row_index.hpp`) | `placeDistinct` under `NDEBUG` / `insertAll` with the repeat check otherwise | the build type |
| `RowIndex::built` | the first lookup that needs the hash builds it; lookups on other threads wait for it (`std::call_once`) | lookups on several threads before the hash is built |
| `RowIndex` hints | the row sumExpr published, the front hint, one of the other three (`LastRows`), the row after it on a walk, the first row before the hash, the hash | the order of lookups |
| `RowIndex::positionOf` | a hint at any byte of another set's rows, left by a destroyed set, rounded down to its row | memory handed back at once (ASan without quarantine) |
| `RowIndex` | one entity column in its `EntityVector`'s order: the row from the address | the key type and the rows |

The thresholds are reached by the large cases and the bench; the runs and the counting sort by the differential
tests. The hints are reached by `tests/stress/test_row_hints.cpp`, which plants one at every byte of the rows and just
outside them, in each of the four slots and as the published row, over keys of every size from 4 to 80 bytes, makes
sets and destroys them so that the hints they left lie in other sets' rows, and looks up on eight threads before the
hash is built; by `tests/unit/test_vartable.cpp` and `tests/unit/test_sum.cpp` on fixed data; and by the differential
tests over random data. The sanitize presets run the stress tests once more without ASan's quarantine. The row from an
entity's address, the hash for the same entities in another order, and a `Ref` of another vector are reached by
`tests/unit/test_vartable.cpp`.

### Operations on a built set

`algebra.hpp`, `ordered.hpp`, `aggregate.hpp` and the backends' shared code (`var_table_core.hpp`, `sum_builder.hpp`,
`row_values.hpp`) branch by the call and by small data, each operation on its own.

| Branch | Reached by |
|---|---|
| unite, intersect, difference, within, `==`, a right operand of reordered columns | the differential tests |
| two sets that share their rows: `everyRowIn` answers without an index | `tests/unit/test_index_set.cpp` |
| select's first occurrences, `select(expr)`, membership and its opposite, an integer key out of its column's range | the differential tests |
| `orderBy` both ways, stable | the differential tests |
| `Ordered` over the set and within groups: steps either way, cyclic, past the span, an absent key, a group's ends | the differential tests |
| step counts of the extreme integers | `tests/unit/test_index_set.cpp` |
| aggregates over a set, a group's rows, a container of one column, no rows | the differential tests |
| a sum over a group of the table's own set (by position) or of an equal set made apart (looked up); a table or a callable as the term; `whereEach`; a numeric term | the differential tests, `tests/unit/test_sum.cpp` |
| a `VarTable` lookup of a missing key | `tests/unit/test_vartable.cpp` |
| a `ParamTable<bool>`'s cursor | `tests/unit/test_vartable.cpp` |

## Why this is enough

- **A source is handled from its own situation.** `generate` binds one source at a time; what passes to the next is
  the list of bound values, which `RemapToBound`, `emit` and `boundKey` handle with one expression per value. The join
  indexes are built for each source before generation (`indexJoins`). The one state shared across sources and rows is
  the debug build's `SourceChecks`, which decides whether a contract check runs again, never which rows come out; the
  contract checks are `contract_fail`'s. So each situation reached once is reached in every combination.
- **The number of columns adds no branch.** The same expression handles each column, so a shape needs only the columns
  its situation needs: three at most (C2 joined on two columns and adding one; a lateral source reading two). The
  shapes have at most four. The 64-column limit is a value range, checked by `compile_fail`.
- **The planner is not a target.** Any visit order gives the same set, and one that reads an unbound column does not
  compile (`generate`'s `static_assert`). The order does decide a source's situation, so each shape pins its order with
  a `static_assert` on `visitOrder()`: a planner that changes stops the shape from compiling before it stops reaching
  its situation. The text of `visitOrder()` is the unit tests'.
- **The value type matters at F alone.** `toColumn`'s branch for the same type is one branch whatever the type; the
  rest that differs by type is in the layouts.
- **Data matters at the index layer alone.** A small differential case (values 0 to 4, at most 125 rows) or a shape's
  data reaches every branch above it but none of the index layer's thresholds; the large cases reach them in every
  CI run of the release presets, and the bench reaches them at scale. A shape's draws are cheap - its compile is
  nearly all of its cost - so each shape runs a few hundred, which covers empty sources, single elements, keys found
  and missed, conditions true and false.
- **The operations branch locally**, and the table above names the test that reaches each branch.

## What is not done, and why

- **No long runs of small random cases.** Their data is a handful of values; after the hundreds CI runs, more seeds
  reach no branch that the first ones did not. The rare paths are behind the index layer's thresholds, which the
  large cases and the bench reach directly.
- **No randomly generated shapes.** The situations are finite and listed above; a random shape is another combination
  of them, which locality makes redundant.
- **No shapes of many columns.** See the column count above.
- **No test of the planner's choices.** See the planner above.

## Checking again after a change

- **The engine, the chain, set-builder notation** (`set.hpp`, `relational.hpp`, `set_builder.hpp`, `element.hpp`,
  `binding.hpp`): look for a new `if constexpr` or a new kind of source. A new situation gets a letter here and a shape
  in `test_shapes.cpp`, with its visit order pinned. A shape whose `static_assert` fails no longer reaches its
  situation: change the shape until it does, not the assertion alone.
- **Filling a column** (`toColumn`, `AmongListOf`): a new branch is a new F, reached by a shape.
- **An index** (`flat_index.hpp`, `grouped.hpp`, `row_index.hpp`): check the thresholds named above against the code,
  and that the large cases still cross them - `test_differential` prints its largest set. A new place for a hint, or a
  new way of checking one, goes into `test_row_hints.cpp`'s sweep.
- **An operation on a built set**: name the test that reaches each new branch in the table above, a unit test where
  small fixed data shows it best.
- **A new kind of value a column holds**: a layout of its own in `tests/stress/test_differential_*.cpp`.
