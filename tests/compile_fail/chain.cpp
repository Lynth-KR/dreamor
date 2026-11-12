// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <cstdint>
#include <functional>
#include <tuple>
#include <vector>

#ifdef DREAMOR_CASE_cross_column_is_there_already
// expect: start a new column, and the set has this one already
int main() {
  const auto ij = IJ();
  (void)dreamor::index_set::relational::from(ij).cross<cI>(I).build();
}
#endif  // DREAMOR_CASE_cross_column_is_there_already

#ifdef DREAMOR_CASE_cross_column_takes_values
// expect: from<C> and cross<C> take a range of values for one column
int main() {
  const auto ij = IJ();
  (void)dreamor::index_set::relational::from<cK>(I).cross<cI>(ij).build();
}
#endif  // DREAMOR_CASE_cross_column_takes_values

#ifdef DREAMOR_CASE_cross_column_from_a_set_of_another_type
// expect: from<C> and cross<C> take a range of values for one column
DREAMOR_VALUE_COLUMN(cShort, std::uint16_t);

int main() {
  const auto sI = dreamor::index_set::relational::from<cI>(I).build();
  (void)dreamor::index_set::relational::from<cK>(I).cross<cShort>(sI).build();
}
#endif  // DREAMOR_CASE_cross_column_from_a_set_of_another_type

#ifdef DREAMOR_CASE_cross_set_shares_a_column
// expect: cross(set) adds new columns, and this set shares a column with the rows so far
int main() {
  const auto ij = IJ();
  (void)dreamor::index_set::relational::from<cI>(I).cross(ij).build();
}
#endif  // DREAMOR_CASE_cross_set_shares_a_column

#ifdef DREAMOR_CASE_from_entity_vector_is_temporary
// expect: An EntityVector handed to from, cross or in must be a named variable
struct Thing {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cThing, Thing);

inline dreamor::EntityVector<Thing> things() {
  dreamor::EntityVector<Thing> made;
  static_cast<void>(made.emplace(Thing{1}));
  made.freeze();
  return made;
}

int main() {
  (void)dreamor::index_set::relational::from<cThing>(things()).build();
}
#endif  // DREAMOR_CASE_from_entity_vector_is_temporary

#ifdef DREAMOR_CASE_from_needs_a_column
// expect: Name the column: from<cMachine>(machines)
int main() {
  (void)dreamor::index_set::relational::from<std::size_t>(I).build();
}
#endif  // DREAMOR_CASE_from_needs_a_column

#ifdef DREAMOR_CASE_from_set_takes_rows
// expect: from(set), cross(set) and join(set) take a set, or a range of rows of columns
int main() {
  (void)dreamor::index_set::relational::from(I).build();
}
#endif  // DREAMOR_CASE_from_set_takes_rows

#ifdef DREAMOR_CASE_from_takes_a_range
// expect: from<C> and cross<C> take a range, the same for every row
int main() {
  const auto next = [](cI i) { return std::vector<std::size_t>{i.get() + 1}; };
  (void)dreamor::index_set::relational::from<cJ>(next).build();
}
#endif  // DREAMOR_CASE_from_takes_a_range

#ifdef DREAMOR_CASE_join_set_shares_no_column
// expect: join(set) matches rows on the columns it shares with the rows so far, and this set shares none
int main() {
  const auto ij = IJ();
  (void)dreamor::index_set::relational::from<cK>(I).join(ij).build();
}
#endif  // DREAMOR_CASE_join_set_shares_no_column

#ifdef DREAMOR_CASE_lateral_is_a_function_name
// expect: lateral<C> cannot take a bare function name or a std::function
std::vector<std::size_t> next(cI i) {
  return {i.get() + 1};
}

int main() {
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cJ>(next).build();
}
#endif  // DREAMOR_CASE_lateral_is_a_function_name

#ifdef DREAMOR_CASE_lateral_is_a_std_function
// expect: lateral<C> cannot take a bare function name or a std::function
int main() {
  const std::function<std::vector<std::size_t>(cI)> next = [](cI i) { return std::vector<std::size_t>{i.get() + 1}; };
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cJ>(next).build();
}
#endif  // DREAMOR_CASE_lateral_is_a_std_function

#ifdef DREAMOR_CASE_lateral_reads_a_later_column
// expect: lateral and where read only the columns before them in the chain
int main() {
  const auto next = [](cI i) { return std::vector<std::size_t>{i.get() + 1}; };
  (void)dreamor::index_set::relational::from<cK>(I).lateral<cJ>(next).cross<cI>(I).build();
}
#endif  // DREAMOR_CASE_lateral_reads_a_later_column

#ifdef DREAMOR_CASE_lateral_reads_no_column
// expect: lateral<C> reads no column, so its list is the same for every row
int main() {
  const auto every = [] { return std::vector<std::size_t>{1, 2}; };
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cJ>(every).build();
}
#endif  // DREAMOR_CASE_lateral_reads_no_column

#ifdef DREAMOR_CASE_lateral_takes_a_callable
// expect: lateral<C> takes a callable over columns before it
int main() {
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cJ>(I).build();
}
#endif  // DREAMOR_CASE_lateral_takes_a_callable

#ifdef DREAMOR_CASE_lateral_takes_column_twice
// expect: lateral<C> takes the same column twice
int main() {
  (void)dreamor::index_set::relational::from<cI>(I)
      .lateral<cK>([](cI a, cI b) { return std::vector<std::size_t>{a.get() + b.get()}; })
      .build();
}
#endif  // DREAMOR_CASE_lateral_takes_column_twice

#ifdef DREAMOR_CASE_chain_over_a_temporary_built_later
// expect: This chain holds a temporary container
std::vector<std::size_t> made();

int main() {
  const auto chain = dreamor::index_set::relational::from<cI>(made());
  return static_cast<int>(chain.build().size());
}
#endif  // DREAMOR_CASE_chain_over_a_temporary_built_later

#ifdef DREAMOR_CASE_chain_over_a_temporary_stepped_later
// expect: This chain holds a temporary container
std::vector<std::size_t> made();

int main() {
  const auto chain = dreamor::index_set::relational::from<cI>(made());
  return static_cast<int>(chain.cross<cJ>(I).build().size());
}
#endif  // DREAMOR_CASE_chain_over_a_temporary_stepped_later

#ifdef DREAMOR_CASE_chain_over_a_braced_list_built_later
// expect: This chain holds a temporary container
int main() {
  const auto chain = dreamor::index_set::relational::from<cI>({1, 2});
  return static_cast<int>(chain.build().size());
}
#endif  // DREAMOR_CASE_chain_over_a_braced_list_built_later

#ifdef DREAMOR_CASE_lateral_takes_a_column_by_reference
// expect: Take each column by value
int main() {
  const auto next = [](cI& /*i*/) -> const std::vector<std::size_t>& { return I; };
  return static_cast<int>(dreamor::index_set::relational::from<cI>(I).lateral<cJ>(next).build().size());
}
#endif  // DREAMOR_CASE_lateral_takes_a_column_by_reference

#ifdef DREAMOR_CASE_lateral_is_generic
// expect: lateral<C> needs one concrete signature
int main() {
  const auto next = [](auto /*i*/) -> const std::vector<std::size_t>& { return I; };
  return static_cast<int>(dreamor::index_set::relational::from<cI>(I).lateral<cJ>(next).build().size());
}
#endif  // DREAMOR_CASE_lateral_is_generic

#ifdef DREAMOR_CASE_lateral_returns_rows_of_two_columns
// expect: lateral<C> returns values for one column, or the rows of a set of one column
int main() {
  const auto ij      = IJ();
  const auto rows_of = [&ij](cI /*i*/) { return ij; };
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cK>(rows_of).build();
}
#endif  // DREAMOR_CASE_lateral_returns_rows_of_two_columns

#ifdef DREAMOR_CASE_lateral_returns_rows_of_another_type
// expect: lateral<C> returns values for one column, or the rows of a set of one column
DREAMOR_VALUE_COLUMN(cShort, std::uint16_t);

int main() {
  const auto sI      = dreamor::index_set::relational::from<cI>(I).build();
  const auto rows_of = [&sI](cK /*k*/) { return sI; };
  (void)dreamor::index_set::relational::from<cK>(I).lateral<cShort>(rows_of).build();
}
#endif  // DREAMOR_CASE_lateral_returns_rows_of_another_type

#ifdef DREAMOR_CASE_lateral_takes_a_value
// expect: lateral<C> takes the columns themselves
int main() {
  const auto next = [](std::size_t /*i*/) -> const std::vector<std::size_t>& { return I; };
  return static_cast<int>(dreamor::index_set::relational::from<cI>(I).lateral<cJ>(next).build().size());
}
#endif  // DREAMOR_CASE_lateral_takes_a_value

#ifdef DREAMOR_CASE_from_rows_of_plain_values
// expect: These rows hold values that are not columns
int main() {
  const std::vector<std::tuple<std::size_t, std::size_t>> rows{{1, 2}};
  return static_cast<int>(dreamor::index_set::relational::from(rows).build().size());
}
#endif  // DREAMOR_CASE_from_rows_of_plain_values

#ifdef DREAMOR_CASE_from_rows_with_a_column_twice
// expect: The rows hold one column type twice
int main() {
  const std::vector<std::tuple<cI, cI>> rows{};
  return static_cast<int>(dreamor::index_set::relational::from(rows).build().size());
}
#endif  // DREAMOR_CASE_from_rows_with_a_column_twice

#ifdef DREAMOR_CASE_from_two_columns
// expect: from<C> names one column
struct Lane {
  std::size_t from, to;
};

int main() {
  const std::vector<Lane> lanes{{1, 2}};
  return static_cast<int>(dreamor::index_set::relational::from<cI, cJ>(lanes).build().size());
}
#endif  // DREAMOR_CASE_from_two_columns
