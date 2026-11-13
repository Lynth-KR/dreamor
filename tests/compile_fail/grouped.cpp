// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_groupby_missing_column
// expect: No column of this set has that name
// when: `groupBy<cK>()` / `select<cK>()` with a foreign column.
//
// fix: Use the set's columns.
int main() {
  (void)IJ().groupBy<cK>();
}
#endif  // DREAMOR_CASE_groupby_missing_column

#ifdef DREAMOR_CASE_grouped_at_takes_a_position
// expect: at() takes a group's position, 0 to size() - 1, not a column
// when: `byI.at(i)` with a column: it would convert to its value and be read as a position.
//
// fix: `byI.slice(i)` for a key's group; `at(g)` with `g` from `0` to `size() - 1`.
int main() {
  const auto sIJ = IJ();
  const auto byI = sIJ.groupBy<cI>();
  (void)byI.at(cI{1});
}
#endif  // DREAMOR_CASE_grouped_at_takes_a_position

#ifdef DREAMOR_CASE_grouped_binding_count
// when: A structured binding over a `Grouped` names other than one name for each key column and one for the rows.
//
// fix: Name each key column, then the rows: `for (const auto& [i, j, rows] : sIJ.groupBy<cI, cJ>())`.
int main() {
  for (const auto& [key, rows] : IJ().groupBy<cI, cJ>()) {
    (void)rows;
  }
}
#endif  // DREAMOR_CASE_grouped_binding_count

#ifdef DREAMOR_CASE_grouped_slice_braced
// expect: without braces
// when: `slice({i, t})`, `S.contains({i, t})`.
//
// fix: `slice(i, t)`, `S.contains(i, t)`.
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.groupBy<cI, cJ>();
  (void)byIJ.slice({cI{2}, cJ{1}});
}
#endif  // DREAMOR_CASE_grouped_slice_braced

#ifdef DREAMOR_CASE_grouped_slice_needs_a_name
// expect: Name the Grouped first
// when: `S.groupBy<C>().slice(c)`.
//
// fix: `const auto byC = S.groupBy<C>();` then `byC.slice(c)`.
int main() {
  (void)IJ().groupBy<cI>().slice(cI{1});
}
#endif  // DREAMOR_CASE_grouped_slice_needs_a_name

#ifdef DREAMOR_CASE_grouped_slice_raw_values
// expect: Slice by this groupBy's key columns
// when: `slice(2, 1)` where two key columns share the values' type, or the key's columns in another order.
//
// fix: Pass the `groupBy` key columns in order; where two share a type, name each value's column (`cFrom{a}, cTo{b}`).
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.groupBy<cI, cJ>();
  (void)byIJ.slice(2, 1);
}
#endif  // DREAMOR_CASE_grouped_slice_raw_values

#ifdef DREAMOR_CASE_grouped_slice_columns_swapped
// expect: Slice by this groupBy's key columns
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.groupBy<cI, cJ>();
  (void)byIJ.slice(cJ{1}, cI{2});
}
#endif  // DREAMOR_CASE_grouped_slice_columns_swapped

#ifdef DREAMOR_CASE_groupby_without_columns
// expect: groupBy<Columns...>() names the key columns
// when: `S.groupBy<>()`.
//
// fix: `S.groupBy<cMachine>()`.
int main() {
  const auto sIJ = IJ();
  return static_cast<int>(sIJ.groupBy<>().size());
}
#endif  // DREAMOR_CASE_groupby_without_columns

#ifdef DREAMOR_CASE_groupby_a_ref_type
// expect: Name the column: s.groupBy<cMachine>()
// when: `groupBy<Ref<Machine>>()`, `select<int>()`: a type that is not a column.
//
// fix: The column: `groupBy<cMachine>()`, declared once with `DREAMOR_ENTITY_COLUMN`.
struct Thing {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

int main() {
  const auto sIJ = IJ();
  return static_cast<int>(sIJ.groupBy<dreamor::Ref<Thing>>().size());
}
#endif  // DREAMOR_CASE_groupby_a_ref_type

#ifdef DREAMOR_CASE_groupby_same_column_twice
// expect: The same column is named twice
// when: `groupBy<cI, cI>()`, `select<cI, cI>()`.
//
// fix: Name each column once.
int main() {
  const auto sIJ = IJ();
  return static_cast<int>(sIJ.groupBy<cI, cI>().size());
}
#endif  // DREAMOR_CASE_groupby_same_column_twice

#ifdef DREAMOR_CASE_group_get_past_the_rows
// expect: Group: index past the rows
// when: `get<I>()` of a group, as a structured binding calls it, with `I` past the key columns and the rows.
//
// fix: Take one element for each key column, then the rows: `get<0>()` to `get<N>()` for `N` key columns.
int main() {
  for (const auto& group : IJ().groupBy<cI>()) {
    (void)group.get<2>();  // cI is element 0 and the rows element 1
  }
}
#endif  // DREAMOR_CASE_group_get_past_the_rows
