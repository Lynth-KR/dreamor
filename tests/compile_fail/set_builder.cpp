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
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<std::size_t>(in<cI>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_head_is_not_columns

#ifdef DREAMOR_CASE_set_builder_in_names_no_column
// expect: names the columns it binds
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI>(in(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_in_names_no_column

#ifdef DREAMOR_CASE_set_builder_set_under_other_columns
// expect: names the set's own columns, each of them and no other
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto ij = IJ();
  (void)of<cI, cK>(in<cI, cK>(ij)).build();
}
#endif  // DREAMOR_CASE_set_builder_set_under_other_columns

#ifdef DREAMOR_CASE_set_builder_values_bind_two_columns
// expect: bind one column each
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI, cJ>(in<cI, cJ>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_values_bind_two_columns

#ifdef DREAMOR_CASE_set_builder_one_column_set_of_another_type
// expect: fills a column of another name only when both hold the same type
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);

int main() {
  using namespace dreamor::index_set::set_builder;
  const auto ii = dreamor::index_set::relational::from<cI>(I).build();
  (void)of<cGrade>(in<cGrade>(ii)).build();
}
#endif  // DREAMOR_CASE_set_builder_one_column_set_of_another_type

#ifdef DREAMOR_CASE_set_builder_reads_a_later_column
// expect: reads a column no generator before it binds
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto j_of = [](cI i) { return std::vector<std::size_t>{i.get()}; };
  (void)of<cI, cJ>(in<cJ>(j_of), in<cI>(I)).build();
}
#endif  // DREAMOR_CASE_set_builder_reads_a_later_column

#ifdef DREAMOR_CASE_set_builder_callable_is_generic
// expect: needs f's columns, written out
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto j_of = [](auto i) { return std::vector<std::size_t>{i.get()}; };
  (void)of<cI, cJ>(in<cI>(I), in<cJ>(j_of)).build();
}
#endif  // DREAMOR_CASE_set_builder_callable_is_generic

#ifdef DREAMOR_CASE_set_builder_callable_is_a_function_name
// expect: cannot take a bare function name or a std::function
std::vector<std::size_t> jOf(cI i);

int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI, cJ>(in<cI>(I), in<cJ>(jOf)).build();
}
#endif  // DREAMOR_CASE_set_builder_callable_is_a_function_name

#ifdef DREAMOR_CASE_set_builder_callable_reads_nothing
// expect: with f reading no column is the same list for every row
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto every = [] { return std::vector<std::size_t>{1}; };
  (void)of<cI, cJ>(in<cI>(I), in<cJ>(every)).build();
}
#endif  // DREAMOR_CASE_set_builder_callable_reads_nothing

#ifdef DREAMOR_CASE_set_builder_source_is_neither
// expect: takes a set, a range of values or a callable
int main() {
  using namespace dreamor::index_set::set_builder;
  (void)of<cI>(in<cI>(std::size_t{1})).build();
}
#endif  // DREAMOR_CASE_set_builder_source_is_neither

#ifdef DREAMOR_CASE_set_builder_named_over_a_temporary
// expect: This set reads a temporary container
std::vector<std::size_t> made();

int main() {
  using namespace dreamor::index_set::set_builder;
  const auto plan = of<cI>(in<cI>(made()));
  return static_cast<int>(plan.build().size());
}
#endif  // DREAMOR_CASE_set_builder_named_over_a_temporary

#ifdef DREAMOR_CASE_set_builder_condition_off_the_head
// expect: A condition takes the head's columns
int main() {
  using namespace dreamor::index_set::set_builder;
  const auto big = [](cJ j) { return j.get() > 1; };
  (void)of<cI>(in<cI>(I)).where(big).build();
}
#endif  // DREAMOR_CASE_set_builder_condition_off_the_head
