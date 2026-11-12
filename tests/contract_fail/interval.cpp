// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <limits>

#ifdef DREAMOR_CASE_interval_closed_at_largest
// expect: interval: closed at its integer type's largest value
// contract: interval_closed_below_largest
int main() try {
  // no value after the last one: the interval would hold every std::size_t, or none
  constexpr std::size_t kLargest = std::numeric_limits<std::size_t>::max();
  const auto sI = dreamor::index_set::relational::from<cI>(dreamor::interval::closed(std::size_t{0}, kLargest)).build();
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_interval_closed_at_largest

#ifdef DREAMOR_CASE_interval_end_out_of_range
// expect: interval: an end its interval's integer type cannot hold
// contract: interval_ends_in_common_type
int main() try {
  const auto sI =
      dreamor::index_set::relational::from<cI>(dreamor::interval::closed(-1, 3U)).build();  // the ends meet in unsigned
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_interval_end_out_of_range

#ifdef DREAMOR_CASE_interval_value_out_of_range
// expect: a value the column's integer type cannot hold
// contract: column_integer_in_range
int main() try {
  const auto sI =
      dreamor::index_set::relational::from<cI>(dreamor::interval::closed(-1, 3)).build();  // cI holds std::size_t
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_interval_value_out_of_range
