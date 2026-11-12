// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <ranges>
#include <tuple>

#ifdef DREAMOR_CASE_aggregate_rows_are_not_columns
// expect: These rows are not rows of columns
int main() {
  const std::vector<std::size_t> values{1, 2};
  const auto big = [](cI i) { return i.get() > 1; };
  (void)dreamor::countIf(values, big);
}
#endif  // DREAMOR_CASE_aggregate_rows_are_not_columns

#ifdef DREAMOR_CASE_max_element_of_a_temporary
// expect: hand back a row by its address: name the set first
int main() {
  const auto big   = [](cI i) { return i.get() > 1; };
  const auto value = [](cJ j) { return j.get(); };
  (void)dreamor::maxElement(IJ().where(big), value);
}
#endif  // DREAMOR_CASE_max_element_of_a_temporary

#ifdef DREAMOR_CASE_aggregate_of_a_number
// expect: take rows: a set, a group's rows
int main() {
  return dreamor::allOf(5, [](cI) { return true; }) ? 0 : 1;
}
#endif  // DREAMOR_CASE_aggregate_of_a_number

#ifdef DREAMOR_CASE_max_element_of_made_rows
// expect: these rows are made as they are read
int main() {
  const auto sIJ      = IJ();
  const auto copy_row = [](const std::tuple<cI, cJ>& row) { return row; };
  const auto made     = std::views::transform(sIJ, copy_row);
  return dreamor::maxElement(made, [](cI) { return 1.0; }) == nullptr ? 0 : 1;
}
#endif  // DREAMOR_CASE_max_element_of_made_rows

#ifdef DREAMOR_CASE_aggregate_container_of_two_columns
// expect: A plain container is one column: name it
int main() {
  const auto i_value = [](cI i) { return i.get(); };
  return static_cast<int>(dreamor::sumValue<cI, cJ>(I, i_value));
}
#endif  // DREAMOR_CASE_aggregate_container_of_two_columns
