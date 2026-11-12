// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_membership_raw_values
// expect: Ask membership
int main() {
  const auto sIJ   = IJ();
  const auto in_ij = sIJ.membership();
  (void)in_ij(1, 2);
}
#endif  // DREAMOR_CASE_membership_raw_values

#ifdef DREAMOR_CASE_set_contains_values_of_one_type
// expect: Ask membership
int main() {
  (void)IJ().contains(1, 2);
}
#endif  // DREAMOR_CASE_set_contains_values_of_one_type

#ifdef DREAMOR_CASE_set_contains_columns_swapped
// expect: Ask membership
int main() {
  (void)IJ().contains(cJ{2}, cI{1});
}
#endif  // DREAMOR_CASE_set_contains_columns_swapped

#ifdef DREAMOR_CASE_set_contains_braced
// expect: without braces
int main() {
  (void)IJ().contains({cI{1}, cJ{2}});
}
#endif  // DREAMOR_CASE_set_contains_braced

#ifdef DREAMOR_CASE_orderby_direction_is_a_tag
// expect: the direction is dreamor::ascending or dreamor::descending
int main() {
  (void)IJ().orderBy([](cI i) { return i.get(); }, true);
}
#endif  // DREAMOR_CASE_orderby_direction_is_a_tag

#ifdef DREAMOR_CASE_orderby_key_is_generic
// expect: Cannot read this callable's parameter list
int main() {
  (void)IJ().orderBy([](const auto& row) { return std::get<cI>(row).get(); });
}
#endif  // DREAMOR_CASE_orderby_key_is_generic

#ifdef DREAMOR_CASE_select_is_not_generic
// expect: Cannot read this callable's parameter list
int main() {
  (void)IJ().select([](auto i, auto j) { return std::tuple<cI, cJ>{j, i}; });
}
#endif  // DREAMOR_CASE_select_is_not_generic

#ifdef DREAMOR_CASE_select_missing_column
// expect: No column of this set has that name
int main() {
  (void)IJ().select<cK>();
}
#endif  // DREAMOR_CASE_select_missing_column

#ifdef DREAMOR_CASE_select_returns_column_twice
// expect: select(expr) returns the same column twice
int main() {
  (void)IJ().select([](cI i) { return std::tuple<cI, cI>{i, i}; });
}
#endif  // DREAMOR_CASE_select_returns_column_twice

#ifdef DREAMOR_CASE_select_returns_values
// expect: select(expr) returns the new row's columns
int main() {
  (void)IJ().select([](cI i, cJ j) { return std::tuple{j.get(), i.get()}; });
}
#endif  // DREAMOR_CASE_select_returns_values

#ifdef DREAMOR_CASE_set_is_read_only
int main() {
  auto s = IJ();
  s.push_back({});
}
#endif  // DREAMOR_CASE_set_is_read_only

#ifdef DREAMOR_CASE_select_without_columns
// expect: select<Columns...>() names the columns it keeps
int main() {
  return static_cast<int>(IJ().select<>().size());
}
#endif  // DREAMOR_CASE_select_without_columns

#ifdef DREAMOR_CASE_difference_of_other_columns
// expect: unite, intersect, difference and within take two sets of the same columns
int main() {
  const auto sI = dreamor::index_set::relational::from<cI>(I).build();
  return static_cast<int>(dreamor::difference(IJ(), sI).size());
}
#endif  // DREAMOR_CASE_difference_of_other_columns

#ifdef DREAMOR_CASE_closure_of_three_columns
// expect: transitiveClosure takes a relation of two columns, from a node to a node
int main() {
  const auto ijk = dreamor::index_set::relational::from(IJ()).cross<cK>(I).build();
  return static_cast<int>(dreamor::transitiveClosure(ijk).size());
}
#endif  // DREAMOR_CASE_closure_of_three_columns

#ifdef DREAMOR_CASE_closure_of_two_kinds
// expect: transitiveClosure takes a relation from a node to a node of the same kind
DREAMOR_VALUE_COLUMN(cName, std::string_view);

int main() {
  const auto named = dreamor::index_set::relational::from<cI>(I).cross<cName>({std::string_view{"a"}}).build();
  return static_cast<int>(dreamor::transitiveClosure(named).size());
}
#endif  // DREAMOR_CASE_closure_of_two_kinds
