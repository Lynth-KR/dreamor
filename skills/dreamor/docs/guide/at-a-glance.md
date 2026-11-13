<!-- Copied from docs/guide/at-a-glance.md (blob 04da3a64fc40) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# At a glance

The names that you type to write a model with dreamor, on one page. The model that the guide's pages build is
described in the [guide's introduction](index.md); each name below links to the page or the section that shows it.

## To start

To write your first model, you need these names alone. Hold everything else in `auto`: what a chain returns, a group,
a sum before `build()`. A column reads as the value it holds, as in `m->capacity` and `t > 1`.

| To | Write |
|---|---|
| hold each entity once | a `primaryKey()` on your struct, `EntityVector`, `insert`, `freeze`, `at`, `Ref` ([Entities](entities.md)) |
| name the places of a row | `DREAMOR_ENTITY_COLUMN`, `DREAMOR_VALUE_COLUMN` ([Columns](columns.md)) |
| build a set | `from`, `lateral`, `cross`, `build`, `interval::closed` ([Relational algebra](relational-algebra.md)), or `of` and `in` if you would rather write it as mathematics ([Set-builder notation](set-builder-notation.md)) |
| take a set's rows in groups | `groupBy`, `slice` ([Grouping](grouping.md)) |
| write the model on HiGHS | `HighsEnv`, `VarTable`, `sumExpr`, `HighsLinearExpression`, `addConstr`, `minimize` ([Variable families](variable-families.md), [Sums](sums.md), [Constraints and objective](constraints-and-objective.md)) |
| read the plan | `solution` ([Solutions](solutions.md)) |

On Concert and MathOpt, the rows and the solve are the solver's own calls, so fewer of these names are dreamor's
([Backends](backends.md#write-the-plan-on-concert-or-mathopt)).

## As you need them

Learn the next names when you need them:

| When you need to | Write |
|---|---|
| keep the rows that meet a condition | [`where`](deriving-sets.md#keep-the-rows-that-meet-conditions) |
| match rows with another set | [`join`](relational-algebra.md#match-rows-with-a-set) |
| keep some columns | [`select`](deriving-sets.md#keep-some-columns) |
| ask whether a row is in a set | [`contains`](deriving-sets.md#ask-whether-a-row-is-in-a-set) |
| look up an entity that may be missing | [`find`](entities.md#look-an-entity-up-by-its-key) |
| hold data on the rows of a set | [`ParamTable`](data-on-a-set.md#put-a-value-on-each-row) |
| combine two sets, or find every part below an item | [`unite`](set-algebra.md#combine-two-sets), [`transitiveClosure`](set-algebra.md#find-every-part-below-an-item) |
| read the period before | [`ordered()`, `prev`](ordered-sets.md#read-the-period-before-where-there-may-be-none) |
| split one sum into several | [`whereEach`](sums.md#add-up-part-of-the-rows-or-split-them-among-sums) |
| name a row, log a message | [`indexedName`](names-and-logging.md#name-a-row-by-its-keys), [`DREAMOR_LOG_INFO`](names-and-logging.md#log-a-models-own-messages) |

Every other name is in the [API reference](../reference/index.md), for when a page sends you there.

## When the build stops

When dreamor stops a build, the compiler's first error is one sentence of dreamor's that says what to write instead.
Search [Compile-time messages](../reference/compile-time-messages.md) for its words: each message is there whole, with
when it comes, what to write, and the cases that pin it. A debug build that stops at run time names a contract, which
[Debug-build checks](../reference/debug-build-checks.md) explains, and a call that throws is in
[Exceptions](../reference/exceptions.md).
