// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_ordered_first_of_absent_group
// expect: Ordered::first has no row to return
// contract: ordered_group_has_row
int main() try {
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sII     = dreamor::index_set::relational::from<cI>(values).cross<cJ>(values).build();
  const auto withinI = sII.orderedWithin<cI>();
  return static_cast<int>(std::get<cJ>(withinI.first(cI{4})).get());  // no row has i = 4: the group is not there
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_ordered_first_of_absent_group

#ifdef DREAMOR_CASE_ordered_first_of_empty_set
// expect: Ordered::first has no row to return - a broken contract: the set is empty
// contract: ordered_set_has_row
int main() try {
  const std::vector<std::size_t> values{};
  const auto sI       = dreamor::index_set::relational::from<cI>(values).build();
  const auto orderedI = sI.ordered();
  return static_cast<int>(std::get<cI>(orderedI.first()).get());  // an empty set has no first row
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_ordered_first_of_empty_set

#ifdef DREAMOR_CASE_ordered_prev_of_first
// expect: Ordered::prevOf has no row to return
// contract: ordered_neighbour_is_there
int main() try {
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sI       = dreamor::index_set::relational::from<cI>(values).build();
  const auto orderedI = sI.ordered();
  return static_cast<int>(std::get<cI>(orderedI.prevOf(cI{1})).get());  // 1 is the first: nothing comes before it
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_ordered_prev_of_first
