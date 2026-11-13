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
// when: `in_ij(1, 2)` or `sIJ.contains(1, 2)` where two columns share the values' type, or the columns in another
// order.
//
// fix: Pass the columns in order, or a row; where two share a type, name each value's column (`cFrom{a}, cTo{b}`).
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
// when: `S.orderBy(key, true)`.
//
// fix: `S.orderBy(key, dreamor::descending)`.
int main() {
  (void)IJ().orderBy([](cI i) { return i.get(); }, true);
}
#endif  // DREAMOR_CASE_orderby_direction_is_a_tag

#ifdef DREAMOR_CASE_orderby_key_is_generic
// expect: Cannot read this callable's parameter list
// when: A generic lambda as a predicate, a sum term, a bound, a `select(expr)` or an `orderBy` key - or a callable
// whose body does not compile, which then is the error before this one.
//
// fix: Fix the body; give the callable column-typed parameters: `[](cMachine m, cPeriod t)`, or name the columns a
// generic one reads: `dreamor::reads<cMachine, cPeriod>(f)`.
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
// when: `std::tuple<cI, cI>{...}`.
//
// fix: Give each column of the new row its own type.
int main() {
  (void)IJ().select([](cI i) { return std::tuple<cI, cI>{i, i}; });
}
#endif  // DREAMOR_CASE_select_returns_column_twice

#ifdef DREAMOR_CASE_select_returns_values
// expect: select(expr) returns the new row's columns
// when: `expr` returns bare values (`std::tuple{j.get(), i.get()}`), or an empty tuple.
//
// fix: Return one `Col` or a `std::tuple` of them: `std::tuple<cFrom, cTo>{j, i}`.
int main() {
  (void)IJ().select([](cI i, cJ j) { return std::tuple{j.get(), i.get()}; });
}
#endif  // DREAMOR_CASE_select_returns_values

#ifdef DREAMOR_CASE_set_is_read_only
// when: A set changed after it is built, such as by `push_back`.
//
// fix: Derive a new set - `where`, `select`, a chain, the set algebra - or assign another set to the name as a whole.
int main() {
  auto s = IJ();
  s.push_back({});
}
#endif  // DREAMOR_CASE_set_is_read_only

#ifdef DREAMOR_CASE_select_without_columns
// expect: select<Columns...>() names the columns it keeps
// when: `S.select<>()`.
//
// fix: `S.select<cMachine, cItem>()`.
int main() {
  return static_cast<int>(IJ().select<>().size());
}
#endif  // DREAMOR_CASE_select_without_columns

#ifdef DREAMOR_CASE_difference_of_other_columns
// expect: unite, intersect, difference and within take two sets of the same columns
// when: `difference(sIJ, sI)`: sets of other columns.
//
// fix: The same columns, in any order; to drop the rows a set of some of the columns holds,
// `S.where(!sOFF.membership())`, to keep them `S.where(sON.membership())`.
int main() {
  const auto sI = dreamor::index_set::relational::from<cI>(I).build();
  return static_cast<int>(dreamor::difference(IJ(), sI).size());
}
#endif  // DREAMOR_CASE_difference_of_other_columns

#ifdef DREAMOR_CASE_closure_of_three_columns
// expect: transitiveClosure takes a relation of two columns, from a node to a node
// when: `transitiveClosure` of a set of one column, or of three or more.
//
// fix: Pick the two columns first: `transitiveClosure(S.select<cParent, cChild>())`.
int main() {
  const auto ijk = dreamor::index_set::relational::from(IJ()).cross<cK>(I).build();
  return static_cast<int>(dreamor::transitiveClosure(ijk).size());
}
#endif  // DREAMOR_CASE_closure_of_three_columns

#ifdef DREAMOR_CASE_closure_of_two_kinds
// expect: transitiveClosure takes a relation from a node to a node of the same kind
// when: `transitiveClosure` of `IndexSet<cMachine, cItem>`, two columns over other types.
//
// fix: A relation whose columns both hold one type: `IndexSet<cParent, cChild>`, both over `Item`.
DREAMOR_VALUE_COLUMN(cName, std::string_view);

int main() {
  const auto named = dreamor::index_set::relational::from<cI>(I).cross<cName>({std::string_view{"a"}}).build();
  return static_cast<int>(dreamor::transitiveClosure(named).size());
}
#endif  // DREAMOR_CASE_closure_of_two_kinds
