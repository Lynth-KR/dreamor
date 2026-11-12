// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_column_named_from_an_integer_out_of_range
// expect: a value the column's integer type cannot hold
// contract: column_integer_in_range
int main() try {
  const int minus_one = -1;
  const cI i{minus_one};  // cI holds std::size_t
  return static_cast<int>(i.get());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_column_named_from_an_integer_out_of_range

#ifdef DREAMOR_CASE_column_from_integers_out_of_range
// expect: a value the column's integer type cannot hold
// contract: column_integer_in_range
int main() try {
  const std::vector<int> raw{2, -1};  // cI holds std::size_t
  return static_cast<int>(dreamor::index_set::relational::from<cI>(raw).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_column_from_integers_out_of_range
