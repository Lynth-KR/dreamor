# dreamor diagnostics

## Compile-time messages

dreamor stops on misuse with a one-sentence `static_assert`. Match the compiler's **first** error against the
left column. Each message is pinned by a `// expect:` line in `tests/compile_fail/` (Concert) or
`tests/mathopt/compile_fail/` (MathOpt), and the pinned case is named in the last column.

| Message contains | Cause | Fix | Case |
|---|---|---|---|
| `Look a variable or value up by the table's key columns` | `VarTable`/`ValueTable` lookup with bare values, a wrong column, wrong order, or a missing key column | Pass the table's key columns in declaration order, or one row; wrap raw values (`PeriodCol{2}`) | `vartable_call_*`, `valuetable_call_raw_values` |
| `without braces` | `slice({i, t})` | `slice(i, t)` | `grouped_slice_braced` |
| `Slice by this groupBy's key columns` | `slice(2, 1)`, or a column that is not a group key | Pass the `groupBy` key columns in order | `grouped_slice_raw_values`, `grouped_slice_wrong_key` |
| `Name the Grouped first` | `S.groupBy<C>().slice(c)` | `const auto byC = S.groupBy<C>();` then `byC.slice(c)` | `grouped_slice_needs_a_name` |
| `Name the Ordered first` | `S.ordered().prev(t)` | `const auto orderedT = S.ordered();` then `orderedT.prev(t)` | `ordered_needs_a_name` |
| `Name an element of the ordered set` | `orderedT.prev(1)` | `orderedT.prev(PeriodCol{1})` | `ordered_prev_raw_value` |
| `Ask membership` | `in_ij(1, 2)` | Pass columns or a row | `membership_raw_values` |
| `Take the columns themselves` | A callable takes `int` / `const T*` instead of the column type | Make the parameter the column type | `where_takes_columns`, `bound_takes_columns`, `sumof_term_takes_values` |
| `Cannot read this callable's parameter list` | A generic lambda as a sum term or a bound | Column-typed parameters | `sumof_term_is_generic`, `bound_is_not_generic` |
| `Cannot read this predicate's parameter list` | A generic lambda as a predicate | Column-typed parameters, or specialize `dreamor::PredicateColumns` | `where_is_not_generic` |
| `names a column these rows do not have` | A term or bound reads a column absent from the rows | Read only existing columns, or derive a set that has them | `sumof_term_reads_missing_column`, `vartable_bound_reads_missing_column` |
| `names a column this set does not have` | A `where` predicate reads a column absent from the set | Same | `where_reads_missing_column` |
| `No column of this set has that name` | `groupBy<KCol>()` / `select<KCol>()` with a foreign column | Use the set's columns | `groupby_missing_column`, `select_missing_column` |
| `A VarTable is built on a set with exactly its key columns` | The table's keys differ from the set's columns or their order | Match the table to the set, or `select<...>()` to reorder | `vartable_key_order_differs_from_set` |
| `is not an expression yet` | `sumOf(...)` without `.build()` in a comparison, `+=`, or `-` | Add `.build()` | `sumof_without_build`, `sumof_without_build_plus_assign`, `sumof_without_build_reversed`, `mathopt/sumof_without_build` |
| `UnbuiltSum` | `sumOf(...)` without `.build()` assigned to an expression or used as an objective | Add `.build()` | `sumof_without_build_assigned`, `sumof_without_build_objective`, `mathopt/sumof_without_build_assigned` |
| `takes the IloEnv first` | Concert `sumOf(rows, term)` | `dreamor::concert::sumOf(env, rows, term)` | `sumof_without_env` |
| `takes no model` | MathOpt `sumOf(model, rows, term)` | `dreamor::mathopt::sumOf(rows, term)` | `mathopt/sumof_with_model` |
| `split() once` | `.split(...).split(...)` | One `split` with every predicate | `sumof_split_twice` |
| `sumOf takes an index set` | `sumOf(env, plainVector, term)` | `sumOf<Col>(env, plainVector, term)`, or build a set | `sumof_container_needs_column` |
| `Bind the set to a variable first` | `sumOf<Col>(env, std::vector<int>{...}, term)` | Store the container in a named variable | `sumof_container_is_temporary` |
| `More column types than columns to name` | `makeSet<A, B>(oneSource)` | One source per column (a set source covers all its columns) | `makeset_missing_source` |
| `Every column needs a name` | `makeSet<int>(...)` | `dreamor::Col<int, "name">` | `makeset_needs_column_types` |
| `dreamor stops at 64` | A set of more than 64 columns, built at once or by spreading a set into a wider one | Split it into sets that share key columns | `makeset_too_many_columns`, `makeset_too_many_columns_spread` |

Also rejected, with no pinned message:
- copying a `VarTable`, default-constructing one, or calling `VarTable::add`;
- a `std::string` name;
- `bool` as the Concert variable type;
- modifying a set (`push_back`);
- a `where` predicate returning `void`;
- a structured binding over a `Grouped` with the wrong number of names (it must be key columns + 1).

## Runtime exceptions

| Exception | When |
|---|---|
| `std::out_of_range` | `VarTable` or `ValueTable` lookup of a row not in the set (all backends); `Grouped::at`/`keyAt` past the end |
| `std::invalid_argument` | `VarTable` constructor on a set with a repeated row |
| `std::logic_error` | `dreamor::mathopt::VarTable::solution` with no primal feasible solution; `assertUnique` failure (debug builds only) |
| `std::length_error` | more than 2^32 − 2 rows in a `VarTable` or a `groupBy` |

Absences that do not throw:
- `Grouped::slice` of a missing key returns an empty `RowSlice`, whose sum is 0.
- `Ordered::next/prev/nextCyclic/prevCyclic` return `nullptr`.
- `Ordered::ord` returns an empty `std::optional`.
