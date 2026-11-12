// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_max_of_empty
// expect: maxOf has no value to return - a broken contract: the rows are empty
// contract: aggregate_rows_not_empty
int main() try {
  DREAMOR_VALUE_COLUMN(cN, std::size_t);
  const auto value = [](cN n) { return n.get(); };
  const std::vector<std::size_t> nothing;
  const auto none = dreamor::index_set::relational::from<cN>(nothing).build();
  return static_cast<int>(dreamor::maxOf(none, value));
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_max_of_empty
