// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// index_set::set_builder: each mistake stops on a sentence in set-builder notation's words, before the chain it is
// lowered onto would speak in its own.
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>
#include "prelude.hpp"

#ifdef DREAMOR_CASE_set_builder_head_leaves_a_column_out
// expect: The head names every column the generators bind
// when: `of<cI>(in<cI>(I), in<cJ>(J))` - a column bound and left out of the head, or a head column no generator binds.
//
// fix: Name every bound column in the head, each once, in the set's order; to keep fewer columns, build the set and
// `select<...>()` them.
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI>(in<cI>(I), in<cJ>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_head_leaves_a_column_out

#ifdef DREAMOR_CASE_set_builder_head_names_a_column_unbound
// expect: The head names every column the generators bind
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI, cJ, cK>(in<cI>(I), in<cJ>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_head_names_a_column_unbound

#ifdef DREAMOR_CASE_set_builder_head_is_not_columns
// expect: names the set's columns first, in their order
// when: `of<std::size_t>(...)`: a head that is not columns.
//
// fix: `of<cMachine, cItem, cPeriod>(in<...>(...), ...)`.
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<std::size_t>(in<cI>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_head_is_not_columns

#ifdef DREAMOR_CASE_set_builder_in_names_no_column
// expect: names the columns it binds
// when: `in(machines)` or `in<Ref<Machine>>(machines)`.
//
// fix: `in<cMachine>(machines)`.
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI>(in(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_in_names_no_column

#ifdef DREAMOR_CASE_set_builder_set_under_other_columns
// expect: names the set's own columns, each of them and no other
// when: `in<cI, cK>(sIJ)`: columns the set does not have, or not all of its own.
//
// fix: The set's columns in any order, `in<cMachine, cItem>(sROUTES)`; a column of another name binds through its own
// generator, or the set is renamed first with `select`.
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto ij = IJ();
  (void)of<cI, cK>(in<cI, cK>(ij)).build();
}
#endif  // DREAMOR_CASE_set_builder_set_under_other_columns

#ifdef DREAMOR_CASE_set_builder_values_bind_two_columns
// expect: bind one column each
// when: `in<cI, cJ>(values)` or `in<cI, cJ>(f)`.
//
// fix: One column for values or a callable; columns bound together come from a set of them, `in<cMachine,
// cItem>(sROUTES)`.
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI, cJ>(in<cI, cJ>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_values_bind_two_columns

#ifdef DREAMOR_CASE_set_builder_one_column_set_of_another_type
// expect: fills a column of another name only when both hold the same type
// when: `in<cGrade>(sPERIODS)`, a set of one column of another value type.
//
// fix: A column of the type the set's column holds (`in<cLater>(sPERIODS)`, `cLater` of `int` as `cPeriod`).
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);

int main() {
  using namespace dreamor::index_set::set_builder;
  const auto ii = dreamor::index_set::relational::from<cI>(I).build();
  (void)of<cGrade>(in<cGrade>(ii)).build();
}
#endif  // DREAMOR_CASE_set_builder_one_column_set_of_another_type

#ifdef DREAMOR_CASE_set_builder_reads_a_later_column
// expect: reads a column no generator before it binds
// when: `in<cItem>(items_of)` written before `in<cMachine>(machines)`.
//
// fix: Put the generator of each column the callable reads first: `in<cMachine>(machines)` before
// `in<cItem>(items_of)`.
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto j_of = [](cI i) { return std::vector<std::size_t>{i.get()}; };
  (void)of<cI, cJ>(in<cJ>(j_of), in<cI>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_reads_a_later_column

#ifdef DREAMOR_CASE_set_builder_callable_is_generic
// expect: needs f's columns, written out
// when: `in<cJ>([](auto i) { ... })`, a generic or `mutable` lambda.
//
// fix: `[](cMachine m) { ... }`.
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto j_of = [](auto i) { return std::vector<std::size_t>{i.get()}; };
  (void)of<cI, cJ>(in<cI>(I), in<cJ>(j_of)).build();
}
#endif  // DREAMOR_CASE_set_builder_callable_is_generic

#ifdef DREAMOR_CASE_set_builder_callable_is_a_function_name
// expect: cannot take a bare function name or a std::function
// when: `in<cJ>(jOf)` with `jOf` a function, or a `std::function`.
//
// fix: A lambda or a function object.
std::vector<std::size_t> jOf(cI i);

int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI, cJ>(in<cI>(I), in<cJ>(jOf)).build();
}
#endif  // DREAMOR_CASE_set_builder_callable_is_a_function_name

#ifdef DREAMOR_CASE_set_builder_callable_reads_nothing
// expect: with f reading no column is the same list for every row
// when: `in<cJ>([] { return list; })`.
//
// fix: Hand the list itself: `in<cItem>(items)`.
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto every = [] { return std::vector<std::size_t>{1}; };
  (void)of<cI, cJ>(in<cI>(I), in<cJ>(every)).build();
}
#endif  // DREAMOR_CASE_set_builder_callable_reads_nothing

#ifdef DREAMOR_CASE_set_builder_source_is_neither
// expect: takes a set, a range of values or a callable
// when: `in<cI>(1)`: a source that is none of them.
//
// fix: `in<cI>(values)`, `in<cI>({1, 2})`, `in<cMachine, cItem>(sROUTES)`, `in<cItem>(items_of)`.
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI>(in<cI>(std::size_t{1})).build();
}
#endif  // DREAMOR_CASE_set_builder_source_is_neither

#ifdef DREAMOR_CASE_set_builder_named_over_a_temporary
// expect: This set reads a temporary container
// when: `const auto plan = of<cI>(in<cI>(made())); plan.build();` - `build()` or `where` on a named `of(...)` over a
// temporary container or values in braces.
//
// fix: Build the set in the statement that wrote it, or name the container first.
std::vector<std::size_t> made();

int main() {
  using namespace dreamor::index_set::set_builder;
  const auto plan = of<cI>(in<cI>(made()));
  return static_cast<int>(plan.build().size());
}
#endif  // DREAMOR_CASE_set_builder_named_over_a_temporary

#ifdef DREAMOR_CASE_set_builder_condition_off_the_head
// expect: A condition takes the head's columns
// when: `of<cI>(...).where([](cJ j) { ... })`.
//
// fix: A condition over the head's columns.
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto big = [](cJ j) { return j.get() > 1; };
  (void)of<cI>(in<cI>(I)).where(big).build();
}
#endif  // DREAMOR_CASE_set_builder_condition_off_the_head

#ifdef DREAMOR_CASE_set_builder_in_names_a_column_twice
// expect: in<C...>(set) names each column once
// when: `in<cI, cI>(set)`: a generator over a set that names one column twice.
//
// fix: Name each of the set's columns once, in the set's order: `in<cI, cJ>(sIJ)`.
int main() {
  using dreamor::index_set::set_builder::in;
  using dreamor::index_set::set_builder::of;
  return static_cast<int>(of<cI>(in<cI, cI>(IJ())).build().size());
}
#endif  // DREAMOR_CASE_set_builder_in_names_a_column_twice

#ifdef DREAMOR_CASE_set_builder_where_without_a_condition
// expect: where() takes the conditions it applies
// when: Set-builder notation's `where()` with no condition.
//
// fix: Give it the conditions, `.where(fits_batch)` or several at once, or drop the call.
int main() {
  using dreamor::index_set::set_builder::in;
  using dreamor::index_set::set_builder::of;
  return static_cast<int>(of<cI>(in<cI>(I)).where().build().size());
}
#endif  // DREAMOR_CASE_set_builder_where_without_a_condition
