// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_ordered_at_takes_a_position
// expect: at() takes a position, 0 to size() - 1, not a column
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.ordered();
  (void)byIJ.at(cI{1});
}
#endif  // DREAMOR_CASE_ordered_at_takes_a_position

#ifdef DREAMOR_CASE_ordered_first_values_of_one_type
// expect: Name the group by the columns orderedWithin<Columns...>() groups by
int main() {
  const auto sIJK = dreamor::index_set::relational::from<cI>(I).cross<cJ>(I).cross<cK>(I).build();
  const auto byIJ = sIJK.orderedWithin<cI, cJ>();
  static_cast<void>(byIJ.first(1, 2));
}
#endif  // DREAMOR_CASE_ordered_first_values_of_one_type

#ifdef DREAMOR_CASE_ordered_first_value_of_another_type
// expect: Name the group by the columns orderedWithin<Columns...>() groups by
int main() {
  const auto byI = IJ().orderedWithin<cI>();
  static_cast<void>(byI.first(1.0));
}
#endif  // DREAMOR_CASE_ordered_first_value_of_another_type

#ifdef DREAMOR_CASE_ordered_needs_a_name
// expect: Name the Ordered first
int main() {
  const auto sI = dreamor::index_set::relational::from<cI>(I).build();
  (void)sI.ordered().prev(cI{1});
}
#endif  // DREAMOR_CASE_ordered_needs_a_name

#ifdef DREAMOR_CASE_ordered_prev_values_of_one_type
// expect: Name an element of the ordered set
int main() {
  const auto sIJ       = IJ();
  const auto orderedIJ = sIJ.ordered();
  (void)orderedIJ.prev(1, 2);
}
#endif  // DREAMOR_CASE_ordered_prev_values_of_one_type

#ifdef DREAMOR_CASE_ordered_position_of_a_text
// expect: Name an element of the ordered set
int main() {
  const auto sI       = dreamor::index_set::relational::from<cI>(I).build();
  const auto orderedI = sI.ordered();
  (void)orderedI.positionOf("1");
}
#endif  // DREAMOR_CASE_ordered_position_of_a_text

#ifdef DREAMOR_CASE_ordered_takes_no_columns
// expect: ordered() takes no columns
int main() {
  const auto sIJ = IJ();
  (void)sIJ.ordered<cI>();
}
#endif  // DREAMOR_CASE_ordered_takes_no_columns

#ifdef DREAMOR_CASE_ordered_whole_set_first_takes_no_group
// expect: first(m) and last(m) name a group's ends
int main() {
  const auto all = IJ().ordered();
  static_cast<void>(all.first(cI{1}));
}
#endif  // DREAMOR_CASE_ordered_whole_set_first_takes_no_group

#ifdef DREAMOR_CASE_ordered_within_has_no_first
// expect: orderedWithin<Columns...>() orders within each group
int main() {
  const auto byI = IJ().orderedWithin<cI>();
  static_cast<void>(byI.first());
}
#endif  // DREAMOR_CASE_ordered_within_has_no_first

#ifdef DREAMOR_CASE_ordered_within_without_columns
// expect: orderedWithin<Columns...>() names the columns
int main() {
  const auto sIJ = IJ();
  return static_cast<int>(sIJ.orderedWithin<>().size());
}
#endif  // DREAMOR_CASE_ordered_within_without_columns
