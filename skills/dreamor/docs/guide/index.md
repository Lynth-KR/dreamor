<!-- Copied from docs/guide/index.md (blob 41c8cf77dffd) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Guide

The guide shows how to do each thing that a model does with dreamor, in code that you can copy, and says what the
code makes. Each page is one topic, and each section on a page is one task. The rules of each call, such as what it
takes, what does not compile and what a debug build checks, are in the [API reference](../reference/index.md).
[At a glance](at-a-glance.md) lists the names that a first model needs, and the ones to learn next.

The code on these pages is under MIT-0: copy it into your model, under any license, with no attribution. The text is
under CC-BY-4.0.

## The model

Every page uses one model, a multi-level lot sizing: a plant plans how much of each item its machines make in each of
three periods.

- The items are the frame, the plate, the bolt, the nut and the bracket. Customers buy frames in every period and
  nuts in period 2. The bracket is no longer sold.
- The bill of materials says which parts each item takes. A frame takes 2 plates and 4 bolts, a plate takes 2 bolts
  and 2 nuts, and a bracket takes a plate and 2 bolts.
- The machine M1 has 4 hours in a period, and makes the frame, the plate and the bracket. The machine M2 has 3 hours
  in a period, and makes the plate, the bolt and the nut.
- The plan decides how much each machine makes of each item in each period, whether the machine is set up for the
  item in that period, and how much of each item is kept at the end of a period. It meets the customers' demand,
  keeps each machine within its hours, and makes the cost of holding stock and of setting machines up as small as
  possible. HiGHS solves it.

## Where each part is shown

| Part of the model | In dreamor | Page |
|---|---|---|
| The items and the machines, each held once | `EntityVector<Item>`, `EntityVector<Machine>`, `Ref` | [Entities](entities.md) |
| A machine, an item or a period as a place in a set's rows | the columns `cMachine`, `cItem`, `cPeriod`, `cParent`, `cChild` | [Columns](columns.md) |
| What each machine can make in each period | a set built by a chain, or written in set-builder notation | [Relational algebra](relational-algebra.md), [Set-builder notation](set-builder-notation.md) |
| The lines of the bill of materials | a set made from records | [Sets from records](sets-from-records.md) |
| Part of a set's rows, some of its columns, its rows sorted | `where`, `select`, `orderBy`, `contains` | [Deriving sets](deriving-sets.md) |
| The items sold, and every part below them | `unite`, `transitiveClosure` | [Set algebra](set-algebra.md) |
| A machine's rows in a period | `groupBy` | [Grouping](grouping.md) |
| The period before a period | `ordered()` | [Ordered sets](ordered-sets.md) |
| A check on the data before the solve | `allOf`, `countIf`, `maxOf`, `findIf` | [Values over a set](values-over-a-set.md) |
| The quantity on each line of the bill of materials | `ParamTable` | [Data on a set](data-on-a-set.md) |
| How much is made, whether a machine is set up, the stock | a `VarTable` for each | [Variable families](variable-families.md) |
| A machine's hours, an item's balance, the cost | `sumExpr` | [Sums](sums.md) |
| A row for each row of a set, and the objective | `addConstr`, `setObjective` | [Constraints and objective](constraints-and-objective.md) |
| The plan that HiGHS finds | `SolutionTable` | [Solutions](solutions.md) |
| The names of the variables and the rows, the model's messages | `indexedName`, `DREAMOR_LOG_INFO` | [Names and logging](names-and-logging.md) |
| The model as HiGHS receives it, and the other solvers | `HighsEnv` | [Backends](backends.md) |

[Cost](cost.md) says what a model built with dreamor costs beyond its solve: the time to write a large model, and
what a set costs to compile.
