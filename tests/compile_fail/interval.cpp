// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <string_view>

#ifdef DREAMOR_CASE_interval_column_is_not_integer
// expect: An interval fills a column of integers
// when: An interval as the source of a column that does not hold an integer.
//
// fix: A column of integers: `DREAMOR_VALUE_COLUMN(cPeriod, int)`.
DREAMOR_VALUE_COLUMN(cName, std::string_view);

int main() {
  (void)dreamor::index_set::relational::from<cName>(dreamor::interval::closed(0, 3)).build();
}
#endif  // DREAMOR_CASE_interval_column_is_not_integer

#ifdef DREAMOR_CASE_interval_ends_are_not_integers
// expect: An interval counts integers
// when: `interval::closed(0.0, 160.0)`.
//
// fix: Integer ends: `interval::closed(0, 160)`.
int main() {
  (void)dreamor::index_set::relational::from<cI>(dreamor::interval::closed(0.0, 3.0)).build();
}
#endif  // DREAMOR_CASE_interval_ends_are_not_integers
