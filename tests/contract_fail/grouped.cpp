// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_slice_front_of_absent_group
// expect: RowSlice::front has no row to return - a broken contract: the slice is empty
// contract: slice_has_row
int main() try {
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sII  = dreamor::index_set::relational::from<cI>(values).cross<cJ>(values).build();
  const auto byI  = sII.groupBy<cI>();
  const auto rows = byI.slice(cI{4});  // no row has i = 4: the slice is empty
  return static_cast<int>(std::get<cJ>(rows.front()).get());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_slice_front_of_absent_group

#ifdef DREAMOR_CASE_slice_back_of_absent_group
// expect: RowSlice::back has no row to return - a broken contract: the slice is empty
// contract: slice_has_row
int main() try {
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sII  = dreamor::index_set::relational::from<cI>(values).cross<cJ>(values).build();
  const auto byI  = sII.groupBy<cI>();
  const auto rows = byI.slice(cI{0});  // no row has i = 0
  return static_cast<int>(std::get<cJ>(rows.back()).get());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_slice_back_of_absent_group
