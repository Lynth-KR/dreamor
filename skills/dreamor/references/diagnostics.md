# dreamor diagnostics

## Compile-time messages

dreamor stops on misuse with a one-sentence `static_assert`. Match the compiler's **first** error against the
left column. Each message is pinned by a `// expect:` line in `tests/compile_fail/` (Concert),
`tests/mathopt/compile_fail/` (MathOpt) or `tests/highs/compile_fail/` (HiGHS), and the pinned case is named in the
last column.

| Message contains | Cause | Fix | Case |
|---|---|---|---|
| `Look a variable or value up by the table's key columns` | `VarTable`/`SolutionTable`/`ParamTable` lookup with a bare value or a column of another name where two key columns share its type (`x(2, 1)` over `cI, cJ` both `std::size_t`), a value of no key column's type, a wrong column, wrong order, or a missing key column | Pass the key's columns in declaration order, or one row; a value goes where its type is one key column's alone - where two share it, name each value's column (`cFrom{a}, cTo{b}`) | `vartable_call_*`, `mathopt/vartable_call_raw_values`, `solutiontable_call_raw_values`, `paramtable_call_raw_values` |
| `A Ref column takes its entities from their EntityVector` | a `Ref` column fed from a `std::vector` of entities or of pointers - a container source, a lateral column's range, or `sumExpr<cX>` or an aggregate such as `countIf<cX>` over a container - or from an `EntityVector` a lateral column returns by value | give the `EntityVector` itself, or the `Ref`s it gave (`at`, `find`) | `ref_column_from_a_vector`, `ref_column_from_pointers`, `ref_column_from_a_returned_vector`, `sumexpr_ref_column_from_a_vector` |
| `A column refers to an entity by a Ref, not a pointer` | `DREAMOR_VALUE_COLUMN(cMachine, const Machine*)` | `DREAMOR_ENTITY_COLUMN(cMachine, Machine)`, filled from the machines' `EntityVector` | `column_holds_a_pointer` |
| `Write Ref<Machine>, not Ref<const Machine>` | `Ref<const Machine>` | `Ref<Machine>`: a `Ref` only reads its entity | `ref_to_a_const_entity` |
| `A Ref refers to an entity: Ref<Machine>, not Ref<Machine*>` | `Ref<Machine*>`, `Ref<Machine&>` | `Ref<Machine>` | `ref_to_a_pointer` |
| `A column identifies its rows by ==: it holds a Ref, an integer` | a column of a type with no `==`, such as a struct of the user's | a `Ref` to an entity, an integer, an enum, a `char`, a text (`std::string_view`, `std::string`), or give the type `==` | `column_holds_a_value_without_equality` |
| `a type of yours needs std::hash as well as ==` | a column of a type of the user's with `==` and no `std::hash` | specialize `std::hash` for the type before the column is declared | `column_holds_a_value_without_hash` |
| `this std::string is a temporary, gone at the end of the statement` | a column of `std::string_view` made from a `std::string` a call returns, `cLabel{trim(field)}` | declare the column over `std::string`, which holds its text, or show the record's own field | `text_column_from_a_temporary_string` |
| `which a floating-point number does not keep` | `DREAMOR_VALUE_COLUMN(cWeight, double)` | an integer, an enum, a `char` or a text; a quantity such as a weight is data on a set (`ParamTable`) or an entity's attribute, not a column; a measured value that is itself a set's member, such as a breakpoint, goes in a type of yours with `==` and `std::hash` | `column_holds_a_double` |
| `A column holds a char, not a wchar_t` | a column of `wchar_t`, `char8_t`, `char16_t` or `char32_t` | a `std::string_view` of the text, or its code as an integer | `column_holds_a_wchar_t` |
| `A column takes values of its own type` | a column of integers filled from a `std::vector<double>`, or any column from values of a type that is not its own | values of the column's type, any integer for a column of integers, text for a column of `std::string_view`; or convert each value on purpose | `integer_column_from_doubles` |
| `A text column shows the text inside its source's std::strings` | a column of `std::string_view` fed from a temporary container of `std::string`s (braced ones too), from a range that makes its strings, or from a lateral source that returns its strings by value | a named container or a view of one (`std::span`); a lateral source that returns a reference (`-> const std::vector<std::string>&`) or a `std::span` | `text_column_from_a_temporary`, `text_column_from_braced_strings`, `text_column_from_a_returned_vector` |
| `This chain holds a temporary container` | `const auto chain = index_set::relational::from<cPeriod>(periods()); chain.build();` - a step or `build()` on a named chain over a temporary container, or over values in braces (`index_set::relational::from<cShift>({...})`) | Build the set in the statement that made the chain, or name the container first: `const auto periodList = periods();` | `chain_over_a_temporary_built_later`, `chain_over_a_temporary_stepped_later`, `chain_over_a_braced_list_built_later` |
| `returns a std::string by value, gone once the call ends` | `ParamTable<std::string_view, cItem>{sITEMS, [](cItem i) { return i->name; }}` - a callable that makes the text it returns | Return a reference to a string that outlives the table: `-> const std::string&`, or a `std::string_view` of it | `paramtable_text_from_a_returned_string` |
| `so the records have to outlive the set` | `IndexSet<cName>{loadRecords(), name_of}`: a set with a text column built from a temporary range of records, a copy, or a range that makes its records | Name the records (`const auto records = loadRecords();`), or hand a view of a named container | `indexset_records_text_from_a_temporary` |
| `row_of takes each record by value` | `IndexSet<cLabel>{records, [](Record r) { return Label{r.name}; }}` - a text column from a copy of each record | Take the record by reference: `[](const Record& r)` | `indexset_records_text_from_a_copied_record` |
| `without braces` | `slice({i, t})`, `S.contains({i, t})` | `slice(i, t)`, `S.contains(i, t)` | `grouped_slice_braced`, `set_contains_braced` |
| `Slice by this groupBy's key columns` | `slice(2, 1)` where two key columns share the values' type, or the key's columns in another order | Pass the `groupBy` key columns in order; where two share a type, name each value's column (`cFrom{a}, cTo{b}`) | `grouped_slice_raw_values`, `grouped_slice_columns_swapped` |
| `Name the Grouped first` | `S.groupBy<C>().slice(c)` | `const auto byC = S.groupBy<C>();` then `byC.slice(c)` | `grouped_slice_needs_a_name` |
| `at() takes a group's position, 0 to size() - 1, not a column` | `byI.at(i)` with a column: it would convert to its value and be read as a position | `byI.slice(i)` for a key's group; `at(g)` with `g` from `0` to `size() - 1` | `grouped_at_takes_a_position` |
| `orderedWithin<Columns...>() orders within each group` | `byA.first()`, `at()`, `last()` on a named `const auto byA = S.orderedWithin<A>();` | `first(a)`, `last(a)` for a group's ends, `positionOf()`, `isFirst()`, `isLast()` within a group, or `S.ordered()` for the whole set | `ordered_within_has_no_first` |
| `first(m) and last(m) name a group's ends` | `orderedS.first(a)` on a named `const auto orderedS = S.ordered();` | `first()` over the whole set, or `S.orderedWithin<A>().first(a)` | `ordered_whole_set_first_takes_no_group` |
| `Name the group by the columns orderedWithin<Columns...>() groups by` | `first(1, 2)` where two group columns share the values' type, a value of no group column's type, or a column that is not a group key | Pass the group's columns in order; where two share a type, name each value's column (`cFrom{a}, cTo{b}`) | `ordered_first_values_of_one_type`, `ordered_first_value_of_another_type` |
| `the direction is dreamor::ascending or dreamor::descending` | `S.orderBy(key, true)` | `S.orderBy(key, dreamor::descending)` | `orderby_direction_is_a_tag` |
| `Name the Ordered first` | `S.ordered().prev(t)` | `const auto orderedT = S.ordered();` then `orderedT.prev(t)` | `ordered_needs_a_name` |
| `at() takes a position, 0 to size() - 1, not a column` | `orderedPeriods.at(t)` with a column: it would be read as a position | `orderedPeriods.positionOf(t)` for its position; `at(n)` with a number | `ordered_at_takes_a_position` |
| `ordered() takes no columns` | `S.ordered<cMachine>()`, positions within groups | `S.orderedWithin<cMachine>()` | `ordered_takes_no_columns` |
| `These rows are not rows of columns` | `countIf(values, pred)` and the like over plain values | Pass a set, a group's rows, or a range of `std::tuple<Cols...>`; or name the container's column, `countIf<cI>(values, pred)` (all but `maxElement`, `minElement`, `findIf`) | `aggregate_rows_are_not_columns` |
| `A plain container is one column: name it` | `sumValue<cI, cJ>(items, f)`, or a `Ref` type in the column's place, for an aggregate over a plain container | Name the one column: `sumValue<cItem>(items, weight)`; several columns are a set, built first | `aggregate_container_of_two_columns` |
| `take rows: a set, a group's rows` | `allOf(5, p)` and the like over something that is not a range | A set, a group's rows (`slice`), or a range of rows | `aggregate_of_a_number` |
| `hand back a row by its address: name the set first` | `maxElement(S.where(p), value)`, a temporary set | `const auto sP = S.where(p);` then `maxElement(sP, value)` | `max_element_of_a_temporary` |
| `these rows are made as they are read` | `const auto made = std::views::transform(s, f);` then `maxElement(made, value)`: rows made on each read have no address to hand back | Iterate a set or a group's rows | `max_element_of_made_rows` |
| `Name an element of the ordered set` | `ordered.prev(1, 2)` where two columns share the values' type, or a value of no column's type (`positionOf("1")`) | Name the columns: `ordered.prev(cI{1}, cJ{2})` | `ordered_prev_values_of_one_type`, `ordered_position_of_a_text` |
| `Ask membership` | `in_ij(1, 2)` or `sIJ.contains(1, 2)` where two columns share the values' type, or the columns in another order | Pass the columns in order, or a row; where two share a type, name each value's column (`cFrom{a}, cTo{b}`) | `membership_raw_values`, `set_contains_values_of_one_type`, `set_contains_columns_swapped` |
| `EntityVector<T> holds each entity once by its primary key` | `EntityVector<T>` for a `T` with no `primaryKey(const T&)` | Define `primaryKey(const T&)` next to `T`, returning its id, code, or a `std::tuple` of them | `entity_vector_needs_primary_key` |
| `A primary key must not lead back to its own type` | a primary key that holds a `Ref` to its own type, such as a part keyed by its assembly, or that leads back to it through another entity's key - in an `EntityVector`, a name or `std::format` | key the entity by a value of its own, such as a code, and keep what it belongs to in a set, such as `IndexSet<cParent, cChild>` | `entity_vector_key_reaches_itself`, `entity_vector_key_reaches_itself_through_another`, `name_key_reaches_itself` |
| `Take the columns themselves` | A callable takes an `int`, a `Ref<T>` or an entity instead of the column type | Make the parameter the column type | `where_takes_columns`, `bound_takes_columns`, `sumexpr_term_takes_values` |
| `Take each column by value` | A predicate, term, bound or lateral source takes `cMachine&` (or `cMachine&&`): a row is read-only | `[](cMachine m)` or `[](const cMachine& m)` | `where_takes_a_column_by_reference`, `chain_where_takes_a_column_by_reference`, `lateral_takes_a_column_by_reference`, `sumexpr_term_takes_a_column_by_reference` |
| `is an indirect call per row` | A function name or a `std::function` as a sum term, a bound, a `ParamTable`'s value, a `select(expr)`, an `orderBy` key or an aggregate's callable | A lambda or a function object | `sumexpr_term_is_a_function_name` |
| `is an indirect call per candidate row` | A function name or a `std::function` as a `where` predicate | A lambda or a function object | `where_is_a_function_name` |
| `This callable reads no columns` | A term, a bound or another column callable with no parameters | Take the columns it reads (`[](cMachine m)`), or use the value itself outside the call | `sumexpr_term_reads_no_column` |
| `The predicate reads no columns` | `S.where([] { return true; })` | Take the columns it tests | `where_reads_no_column` |
| `Cannot read this callable's parameter list` | A generic lambda as a predicate, a sum term, a bound, a `select(expr)` or an `orderBy` key - or a callable whose body does not compile, which then is the error before this one | Fix the body; give the callable column-typed parameters: `[](cMachine m, cPeriod t)` | `sumexpr_term_is_generic`, `bound_is_not_generic`, `select_is_not_generic`, `orderby_key_is_generic`, `where_is_not_generic` |
| `where() takes the predicates it applies` | `S.where()` or a chain's `.where()` with no predicate | `S.where(is_open)`, or several at once | `where_without_a_predicate`, `chain_where_without_a_predicate` |
| `takes a column these rows do not have` | A term or bound reads a column absent from the rows | Read only existing columns, or derive a set that has them | `sumexpr_term_reads_missing_column`, `vartable_bound_reads_missing_column` |
| `takes a column this set does not have` | A `where` predicate reads a column absent from the set | Same | `where_reads_missing_column` |
| `select(expr) returns the new row's columns` | `expr` returns bare values (`std::tuple{j.get(), i.get()}`), or an empty tuple | Return one `Col` or a `std::tuple` of them: `std::tuple<cFrom, cTo>{j, i}` | `select_returns_values` |
| `select(expr) returns the same column twice` | `std::tuple<cI, cI>{...}` | Give each column of the new row its own type | `select_returns_column_twice` |
| `start a new column, and the set has this one already` | `index_set::relational::from<C>`, `cross<C>` or `lateral<C>` names a column the chain has | Give the column its own type (`DREAMOR_ENTITY_COLUMN(cTo, Node);`); to match two sets on a column, `join` | `cross_column_is_there_already` |
| `This callable takes the same column twice` | A predicate, term, bound or `select(expr)` names one column in two parameters (`[](cFrom a, cFrom b)`) | Give each parameter its own column, or take the column once | `where_takes_column_twice`, `sumexpr_term_takes_column_twice` |
| `lateral<C> takes the same column twice` | A lateral source with a repeated column parameter | Give each parameter its own column, or take the column once | `lateral_takes_column_twice` |
| `names one key column twice` | `VarTable<cI, cI>` | Give each key its own column type | `vartable_key_twice` |
| `No column of this set has that name` | `groupBy<cK>()` / `select<cK>()` with a foreign column | Use the set's columns | `groupby_missing_column`, `select_missing_column` |
| `The same column is named twice` | `groupBy<cI, cI>()`, `select<cI, cI>()` | Name each column once | `groupby_same_column_twice` |
| `Name the column: s.groupBy<cMachine>()` | `groupBy<Ref<Machine>>()`, `select<int>()`: a type that is not a column | The column: `groupBy<cMachine>()`, declared once with `DREAMOR_ENTITY_COLUMN` | `groupby_a_ref_type` |
| `select<Columns...>() names the columns it keeps` | `S.select<>()` | `S.select<cMachine, cItem>()` | `select_without_columns` |
| `groupBy<Columns...>() names the key columns` | `S.groupBy<>()` | `S.groupBy<cMachine>()` | `groupby_without_columns` |
| `orderedWithin<Columns...>() names the columns` | `S.orderedWithin<>()` | `S.orderedWithin<cMachine>()`, or `S.ordered()` for the whole set | `ordered_within_without_columns` |
| `A VarTable is built on a set with exactly its key columns` | The table's keys differ from the set's columns or their order | Match the table to the set, or `select<...>()` to reorder | `vartable_key_order_differs_from_set` |
| `A ParamTable is built on a set with exactly its key columns` | A ParamTable whose keys differ from the set's columns or their order | Match the table to the set, or `select<...>()` to reorder | `paramtable_key_order_differs_from_set` |
| `A ParamTable refers to an entity by a Ref, not a pointer` | `ParamTable<const Machine*, ...>` | `ParamTable<dreamor::Ref<Machine>, ...>` | `paramtable_holds_a_pointer` |
| `A ParamTable holds a value a row, copied in` | `ParamTable<const double&, ...>`: a reference, an array or a `const` type | the value type itself: a number, a `bool`, an enum, a text, a `Ref`, or a struct of the row's values (`ParamTable<Lane, cFrom, cTo>`) | `paramtable_holds_a_reference` |
| `value_of takes one record and returns its value` | `ParamTable{records, row_of, value_of}` whose `value_of` does not take the records' element type | Take the record's type, or `const auto&` | `paramtable_records_value_of_takes_another_type` |
| `value_of returns something else than the table's T` | `ParamTable{records, row_of, value_of}` whose `value_of` returns what does not convert to `T` | Return a `T`, or a table of the type it returns | `paramtable_records_value_of_returns_another_type` |
| `reads the records twice, for the rows and for the values` | `ParamTable{records, row_of, value_of}` over a range read once, such as `std::views::istream` | A container of the records, or a view of one | `paramtable_records_read_once` |
| `of texts shows each text where value_of found it` | a `std::string_view` table from temporary records, from a `value_of` that takes the record by value, or returns a `std::string` | Named records taken by reference and the record's own text, or a table over `std::string` | `paramtable_records_text_from_temporary_records` |
| `A column of entities takes a Ref to the entity, not its primary key` | a text in an entity column's key place, `hours("M1")` | Look the entity up: `hours(machines.at("M1"))` | `table_key_is_an_entity_primary_key` |
| `A column of entities takes a Ref to the entity, not the entity object` | an `EntityVector`'s element in an entity column's key place, `for (const Machine& m : machines) hours(m)` | Take its `Ref` from the vector, `machines.ref(m)`, or walk `machines.refs()` or a set of the column | `table_key_is_an_entity_object` |
| `or a callable over the row's columns that returns a T` | A ParamTable's callable returns something not convertible to its `T` | Return a `T` | `paramtable_value_returns_another_type` |
| `A ParamTable takes a value for each row from a callable` | `ParamTable<double, cMachine>{sMACHINES, hours}` with `hours` a container | A callable over the row's columns that reads it: `[&hours](cMachine m) { return hours.at(m->id); }` | `paramtable_value_from_a_vector` |
| `One value for every row is a T itself, not a value that converts to one` | `ParamTable<std::string_view, cMachine>{sMACHINES, label}` with `label` a `std::string` | One value of the table's `T`: `std::string_view{label}`, with `label` outliving the table | `paramtable_value_converts_to_the_type` |
| `This value is not a T, nor a callable` | `ParamTable<std::string_view, cMachine>{sMACHINES, 3}`: one value of another type | one value of the table's `T`, or a callable over the row's columns | `paramtable_value_of_another_type` |
| `A VarTable takes the variables' type before their name` | `VarTable{env, set, lb, ub, "Make"}` on Concert or HiGHS: the name where the type goes | `VarTable{env, set, lb, ub, ILOFLOAT, "Make"}` (Concert), `VarTable{env, set, lb, ub, HighsVarType::kContinuous, "Make"}` (HiGHS) | `vartable_name_in_the_type_place`, `highs/vartable_name_in_the_type_place` |
| `A VarTable takes is_integer before the variables' name` | `VarTable{model, set, lb, ub, "Make"}` on MathOpt, where the name would convert to `is_integer = true` | `VarTable{model, set, lb, ub, false, "Make"}` | `mathopt/vartable_name_in_the_is_integer_place` |
| `A name is text, and nullptr is not one` | `nullptr` as a name - a `VarTable`'s on every backend, HiGHS's `addVariable` or `addConstr`: the name is a `std::string_view`, which a null pointer would crash | Leave the name out, or pass `{}`, for no name | `vartable_null_name`, `highs/vartable_null_name`, `highs/add_variable_null_name`, `highs/add_constr_null_name`, `mathopt/vartable_null_name` |
| `A VarTable's bound is a number or a callable` | `VarTable{env, s, 0, upper}` with `upper` a container | A callable over the row's columns, or a `ParamTable` on the set | `vartable_bound_from_a_vector` |
| `A VarTable's bound is a number, and this callable returns something else` | A bound callable returns a `Ref` or an entity's column: `[](cMachine m) { return m; }` | Return the number: `[](cMachine m) { return m->capacity; }` | `vartable_bound_returns_a_ref` |
| `Scale the sum, not the table` | `100 * vYield` or `vYield * 100` | `100 * sumExpr(env, rows, vYield).build()`, or multiply inside a term | `vartable_scaled`, `mathopt/vartable_scaled`, `highs/vartable_scaled` |
| `is not an expression yet` | `sumExpr(...)` without `.build()` in a comparison, `+=`, `-`, or as a HiGHS objective (`minimize`, `maximize`, `setObjective`) | Add `.build()` | `sumexpr_without_build`, `sumexpr_without_build_plus_assign`, `sumexpr_without_build_reversed`, `mathopt/sumexpr_without_build`, `highs/sumexpr_without_build`, `highs/minimize_sumexpr_without_build`, `highs/set_objective_sumexpr_without_build` |
| `UnbuiltSum` | `sumExpr(...)` without `.build()` assigned to an expression or used as an objective | Add `.build()` | `sumexpr_without_build_assigned`, `sumexpr_without_build_objective`, `mathopt/sumexpr_without_build_assigned` |
| `takes the IloEnv first` | Concert `sumExpr(rows, term)` | `dreamor::concert::sumExpr(env, rows, term)` | `sumexpr_without_env` |
| `takes no model` | MathOpt `sumExpr(model, rows, term)` | `dreamor::mathopt::sumExpr(rows, term)` | `mathopt/sumexpr_with_model` |
| `takes no HighsEnv` | HiGHS `sumExpr(env, rows, term)` | `dreamor::highs::sumExpr(rows, term)` | `highs/sumexpr_with_highs` |
| `HiGHS takes quadratic terms only in the objective` | A quadratic expression in a row (`x * x <= 1`, a quadratic sum compared) | Put the square in `setObjective()`/`minimize()` | `highs/quadratic_row`, `highs/quadratic_sum_row` |
| `addConstr takes a row` | `addConstr(env, x + y)`: an expression with no bounds | `addConstr(env, x + y <= 1)`, or `addConstr(env, (0 <= x - y) <= 5)` | `highs/constr_without_bounds` |
| `An objective is an expression, not a row` | `setObjective(env, x + y <= 1)` | `setObjective(env, x + y)` | `highs/objective_row` |
| `setObjective takes an expression of this backend's variables` | `setObjective(env, x)` with something that is not a HiGHS expression: a set, another backend's expression | A `HighsVar`, a term, a `HighsLinearExpression` or a `HighsQuadraticExpression` | `highs/objective_of_another_backend` |
| `whereEach() once` | `.whereEach(...).whereEach(...)` | One `whereEach` with every predicate | `sumexpr_where_each_twice` |
| `sumExpr takes an index set` | `sumExpr(env, plainVector, term)` | `sumExpr<Col>(env, plainVector, term)`, or build a set | `sumexpr_container_needs_column` |
| `sumExpr sums over a range of rows` | `sumExpr(env, s.ordered(), term)`, `sumExpr(env, *s.begin(), term)`: an `Ordered`, or one row | Sum over the set itself, or a group's rows (`byMachine.slice(m)`); one row is one term | `sumexpr_over_an_ordered`, `sumexpr_over_one_row` |
| `A Grouped is a range of groups, not of rows` | `sumExpr(env, s.groupBy<cMachine>(), term)` | One group's rows, `sumExpr(env, byMachine.slice(m), term)`, or the set itself | `sumexpr_over_a_grouped` |
| `One column type at most` | `sumExpr<cMachine, cItem>(env, machines, term)` | Build a set over several columns, then `sumExpr(env, set, term)` | `sumexpr_container_two_columns` |
| `Name the column: sumExpr<cMachine>` | `sumExpr<Ref<Machine>>(env, machines, cost)`, a type that is not a column | `sumExpr<cMachine>(env, machines, cost)` | `sumexpr_container_column_not_named` |
| `These rows already are an index set` | `sumExpr<cMachine>(env, sASSIGN, term)` over a set | `sumExpr(env, sASSIGN, term)`: a set brings its column types | `sumexpr_set_given_a_column` |
| `These rows are tuples of plain values` | `sumExpr(env, rows, term)` over `std::tuple<int, int>` rows | Build the set with `index_set::relational::from<cMachine>(...).cross<cItem>(...)` | `sumexpr_plain_tuples` |
| `sumExpr keeps its rows by reference, so they cannot be a temporary container` | `sumExpr<Col>(env, std::vector<int>{...}, term)`, or a temporary range of rows | Name the container first, then sum over the name | `sumexpr_container_is_temporary` |
| `An EntityVector handed to from, cross or in must be a named variable` | `index_set::relational::from<cMachine>(...)` over a temporary `EntityVector`: a row refers to its elements | Bind it to a name first; rows of values, `Ref`s or tuples may come from a temporary | `from_entity_vector_is_temporary` |
| `IndexSet{records, row_of} does not deduce its columns` | `dreamor::IndexSet sROUTES{routes, route_of}`: C++20 cannot read the columns off what `row_of` returns | Name the set's type: `IndexSet<cMachine, cItem> sROUTES{routes, route_of}` | `indexset_records_columns_not_deduced` |
| `An IndexSet's type lists its columns` | `IndexSet<int>{records, row_of}`, `IndexSet<Machine, Item>` | `IndexSet<cMachine, cItem>` | `indexset_of_a_non_column` |
| `makes a row of each record: give a range of records` | `IndexSet<...>{x, row_of}` with `x` not a range | Pass a container or a view of the records | `indexset_records_not_a_range` |
| `row_of takes one record and returns its row` | `row_of` does not take the records' element type | Take the record's type, or `const auto&` | `indexset_records_row_of_takes_another_type` |
| `row_of returns other columns than the set's` | `row_of` returns columns the set does not have, or misses one | Return the set's columns, in any order: `std::tuple<cMachine, cItem>{m, i}` | `indexset_records_other_columns` |
| `row_of returns plain values, and a value fills the column of its type` | A plain value two columns could take - two columns of its type, or an integer of another type where the set has two columns of integers - or one no column takes | Return the columns by name: `std::tuple<cFrom, cTo>{a, b}` | `indexset_records_values_share_a_type`, `indexset_records_integer_with_two_integer_columns` |
| `row_of returns a std::string for a column of std::string_view` | `row_of` returns text it made (`trim(field)`, `substr`) for a column of `std::string_view`, which would show it after the call | Declare the column over `std::string`, or return the record's own text | `indexset_records_text_from_a_returned_string` |
| `lateral<C> cannot take a bare function name or a std::function` | A function name or a `std::function` as a lateral source | Pass a lambda or a function object | `lateral_is_a_function_name`, `lateral_is_a_std_function` |
| `An interval counts integers` | `interval::closed(0.0, 160.0)` | Integer ends: `interval::closed(0, 160)` | `interval_ends_are_not_integers` |
| `An interval fills a column of integers` | An interval as the source of a column that does not hold an integer | a column of integers: `DREAMOR_VALUE_COLUMN(cPeriod, int)` | `interval_column_is_not_integer` |
| `Name the column: from<cMachine>(machines)` | `index_set::relational::from<int>(...)`, `cross<Ref<Machine>>(...)` | a column: `DREAMOR_ENTITY_COLUMN(cMachine, Machine);` | `from_needs_a_column` |
| `from<C> and cross<C> take a range, the same for every row` | A callable (or no range) given to `index_set::relational::from<C>`/`cross<C>` | A list per row is `lateral<C>(f)` | `from_takes_a_range` |
| `from<C> and cross<C> take a range of values for one column` | A set of more than one column, or of one column of another value type, given to `index_set::relational::from<C>`/`cross<C>` | `index_set::relational::from(set)`, `cross(set)` or `join(set)`; a set of one column fills `C` when its column holds `C`'s value type | `cross_column_takes_values`, `cross_column_from_a_set_of_another_type` |
| `from(set), cross(set) and join(set) take a set` | A range of values given to `index_set::relational::from(...)`/`cross(...)`/`join(...)`, or a `ParamTable` given as the set | Name its column: `index_set::relational::from<cMachine>(machines)`; a table's set is its `keys()`, `join(cost.keys())` | `from_set_takes_rows` |
| `from<C> names one column` | `index_set::relational::from<cPlant, cCustomer>(lanes)`: several columns from one range | Rows of several columns from data rows: `IndexSet<cPlant, cCustomer>{lanes, lane_of}`, `lane_of` returning a row of each; a product of sources: `from<cPlant>(plants).cross<cCustomer>(customers)` | `from_two_columns` |
| `The head names every column the generators bind` | `of<cI>(in<cI>(I), in<cJ>(J))` - a column bound and left out of the head, or a head column no generator binds | Name every bound column in the head, each once, in the set's order; to keep fewer columns, build the set and `select<...>()` them | `set_builder_head_leaves_a_column_out`, `set_builder_head_names_a_column_unbound` |
| `names the set's columns first, in their order` | `of<std::size_t>(...)`: a head that is not columns | `of<cMachine, cItem, cPeriod>(in<...>(...), ...)` | `set_builder_head_is_not_columns` |
| `names the columns it binds` | `in(machines)` or `in<Ref<Machine>>(machines)` | `in<cMachine>(machines)` | `set_builder_in_names_no_column` |
| `names the set's own columns, each of them and no other` | `in<cI, cK>(sIJ)`: columns the set does not have, or not all of its own | The set's columns in any order, `in<cMachine, cItem>(sROUTES)`; a column of another name binds through its own generator, or the set is renamed first with `select` | `set_builder_set_under_other_columns` |
| `bind one column each` | `in<cI, cJ>(values)` or `in<cI, cJ>(f)` | One column for values or a callable; columns bound together come from a set of them, `in<cMachine, cItem>(sROUTES)` | `set_builder_values_bind_two_columns` |
| `fills a column of another name only when both hold the same type` | `in<cGrade>(sPERIODS)`, a set of one column of another value type | A column of the type the set's column holds (`in<cLater>(sPERIODS)`, `cLater` of `int` as `cPeriod`) | `set_builder_one_column_set_of_another_type` |
| `reads a column no generator before it binds` | `in<cItem>(items_of)` written before `in<cMachine>(machines)` | Put the generator of each column the callable reads first: `in<cMachine>(machines)` before `in<cItem>(items_of)` | `set_builder_reads_a_later_column` |
| `needs f's columns, written out` | `in<cJ>([](auto i) { ... })`, a generic or `mutable` lambda | `[](cMachine m) { ... }` | `set_builder_callable_is_generic` |
| `cannot take a bare function name or a std::function` | `in<cJ>(jOf)` with `jOf` a function, or a `std::function` | A lambda or a function object | `set_builder_callable_is_a_function_name` |
| `with f reading no column is the same list for every row` | `in<cJ>([] { return list; })` | Hand the list itself: `in<cItem>(items)` | `set_builder_callable_reads_nothing` |
| `takes a set, a range of values or a callable` | `in<cI>(1)`: a source that is none of them | `in<cI>(values)`, `in<cI>({1, 2})`, `in<cMachine, cItem>(sROUTES)`, `in<cItem>(items_of)` | `set_builder_source_is_neither` |
| `This set reads a temporary container` | `const auto plan = of<cI>(in<cI>(made())); plan.build();` - `build()` or `where` on a named `of(...)` over a temporary container or values in braces | Build the set in the statement that wrote it, or name the container first | `set_builder_named_over_a_temporary` |
| `A condition takes the head's columns` | `of<cI>(...).where([](cJ j) { ... })` | A condition over the head's columns | `set_builder_condition_off_the_head` |
| `These rows hold values that are not columns` | `index_set::relational::from(rows)`, `cross(rows)` or `join(rows)` over `std::tuple<int, int>` rows | A set, or a range of `std::tuple<cMachine, cItem>` | `from_rows_of_plain_values` |
| `The rows hold one column type twice` | `index_set::relational::from(rows)` over `std::tuple<cI, cI>` rows | Give each column its own type | `from_rows_with_a_column_twice` |
| `lateral<C> takes a callable over columns before it` | A range given to `lateral<C>` | A range the same for every row is `cross<C>(range)` | `lateral_takes_a_callable` |
| `lateral<C> reads no column` | A lateral source with no parameters | `cross<C>(range)` | `lateral_reads_no_column` |
| `lateral<C> needs one concrete signature` | A generic or `mutable` lambda as a lateral source | `[](cMachine m) { ... }`, callable as const | `lateral_is_generic` |
| `lateral<C> returns values for one column, or the rows of a set of one column` | A lateral source returning a set of more columns, or of one column of another value type | Return values, or a set of one column holding `C`'s value type; a set of more columns is `join(set)` | `lateral_returns_rows_of_two_columns`, `lateral_returns_rows_of_another_type` |
| `lateral<C> takes the columns themselves` | A lateral source taking `Ref<Machine>` or a plain value | Column-typed parameters: `[](cMachine m)` | `lateral_takes_a_value` |
| `lateral and where read only the columns before them in the chain` | A lateral source or a chain's `where` reads a column a later step brings | Move the step after the source of that column | `lateral_reads_a_later_column`, `where_reads_a_later_column` |
| `cross(set) adds new columns, and this set shares a column` | `cross(s)` where `s` has a column the chain has | `join(s)` | `cross_set_shares_a_column` |
| `join(set) matches rows on the columns it shares` | `join(s)` where `s` shares no column with the chain | To match on columns of other names, rename them first: `join(sBOM.select(as_item))`, `as_item` returning `std::tuple<cItem, cChild>{parent, child}`; to pair every row with every row, `cross(s)` | `join_set_shares_no_column` |
| `unite, intersect, difference and within take two sets of the same columns` | `difference(sIJ, sI)`: sets of other columns | The same columns, in any order; to drop the rows a set of some of the columns holds, `S.where(!sOFF.membership())`, to keep them `S.where(sON.membership())` | `difference_of_other_columns` |
| `transitiveClosure takes a relation of two columns, from a node to a node` | `transitiveClosure` of a set of one column, or of three or more | Pick the two columns first: `transitiveClosure(S.select<cParent, cChild>())` | `closure_of_three_columns` |
| `transitiveClosure takes a relation from a node to a node of the same kind` | `transitiveClosure` of `IndexSet<cMachine, cItem>`, two columns over other types | A relation whose columns both hold one type: `IndexSet<cParent, cChild>`, both over `Item` | `closure_of_two_kinds` |
| `dreamor stops at 64` | A set of more than 64 columns, built at once or by spreading a set into a wider one | Split it into sets that share key columns | `set_too_many_columns`, `set_too_many_columns_spread` |
| `N is at least 2` | `FormatBuffer<1>`, `NameBuilder<1>` | `N` of 2 or more: room for one character and the null terminator | `format_buffer_of_one` |

Also rejected, with no pinned message:
- copying a `VarTable`, default-constructing one, or calling `VarTable::add`;
- `bool` as the Concert variable type;
- modifying a set (`push_back`);
- a `where` predicate returning `void`;
- a structured binding over a `Grouped` with the wrong number of names (it must be key columns + 1).

## Runtime exceptions

| Exception | When |
|---|---|
| `std::out_of_range` | `VarTable`, `SolutionTable` or `ParamTable` lookup of a row not in the set (all backends), and `EntityVector::at` of a key none has - each names the key, `VarTable: no row (M1, bolt, 4) in the table's set.`; `Grouped::at` and `Ordered::at` past the end |
| `std::runtime_error` | a name longer than its buffer: `indexedName` past 255 characters, `NameBuilder` / `FormatBuffer` past `N - 1` under `OnFormatOverflow::kThrow` |
| `std::logic_error` | `dreamor::mathopt::VarTable::solution` with no primal feasible solution; `dreamor::highs::VarTable::solution` and `dreamor::highs::val` unless the last solve left a feasible primal solution (a re-solve that ends infeasible included) |
| `dreamor::highs::HighsCallError` | a HiGHS call that fails, with `call()` and `status()`; a model HiGHS refuses throws from the first `env.highs()` (`"passModel"`) |
| `std::length_error` | more than 2^32 - 2 rows or elements in an `EntityVector`, a `VarTable`, a `ParamTable`, a `groupBy`, or anything else that hashes rows (`select`, `unite`, `intersect`, `difference`, `membership`, `ordered`, a join in a chain) |

## Broken contracts (debug builds)

A broken precondition is undefined behavior: what is handed over meets it. A debug build (no `NDEBUG`) checks these
and aborts after one line:

| Message starts with | The contract broken |
|---|---|
| `building a set: source N holds one element twice` | a container source holds each value or entity once; an entity is one object in one container |
| `building a set: source N holds one row twice` | a range of tuples handed to `from`, `cross` or `join` holds each row once |
| `building a set: lateral source N returned one element twice` | a lateral source's range holds each element once, for every binding |
| `building a set: source N holds an empty Ref at K` | a container of `Ref`s holds no empty one |
| `building a set: source N holds a row with an empty Ref at K` | a range of rows handed to `from`, `cross` or `join` holds no empty `Ref` |
| `building a set: lateral source N returned an empty Ref at K` | a lateral column's range holds no empty `Ref`, for every binding |
| `IndexSet{records, row_of}, or a ParamTable built from records: records J and K (counting from 0) both give the row (...)` | `row_of` gives each record a row of its own |
| `IndexSet{records, row_of}: row_of returned an empty Ref for record K` | `row_of` returns no empty `Ref` |
| `EntityVector::<member> before freeze()` | an `EntityVector` is read (iteration, a set source, `[]`, `find`) only once frozen |
| `EntityVector::<member> after freeze()` | an `EntityVector` is filled (`reserve`, `insert`/`emplace`, `sort`) only before `freeze()` |
| `Ref: an empty Ref read` | a `Ref` is read (`->`, `*`) only when it refers to an entity |
| `Ordered::nextOf has no row to return` (or `prevOf`) | `nextOf`/`prevOf` are asked of an element that has the neighbour in its set or group |
| `Ordered::first has no row to return` (or `last`) | `first(m)`/`last(m)` are asked of a group with a row in the set, `first()`/`last()` of a set with rows |
| `RowSlice::front has no row to return` (or `back`) | `front()`/`back()` are asked of a slice with rows: a key with a row in the set, or a slice that `empty()` says is not empty |
| `VarTable is assigned over a family` | a family is moved only into a `VarTable` that holds no variables: one constructed with no arguments, or moved from |
| `interval: closed at its integer type's largest value` | an interval's closed last end is below its integer type's largest value |
| `interval: an end its interval's integer type cannot hold` | an interval's ends fit its integer type |
| `maxOf has no value to return` (or `minOf`) | `maxOf`/`minOf` take rows that are not empty |
| `a value the column's integer type cannot hold` | a column's integer type holds every value given to it - an interval's, an integer range's, `cPeriod{k}`'s |
| `dreamor::highs: one expression holds variables of two HighsEnvs` | an expression holds one `HighsEnv`'s variables |
| `dreamor::highs::<call>: the expression's variables belong to another HighsEnv` | a row or an objective goes to the `HighsEnv` its variables belong to |

Absences that do not throw:
- `Grouped::slice` of a missing key returns an empty `RowSlice`, whose sum is 0.
- `Ordered::next/prev/nextCyclic/prevCyclic` return `nullptr`.
- `Ordered::positionOf` returns an empty `std::optional`.
