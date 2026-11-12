// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_vartable_call_raw_values
// expect: Look a variable or value up by the table's key columns
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  (void)x(2, 1);
}
#endif  // DREAMOR_CASE_vartable_call_raw_values

#ifdef DREAMOR_CASE_vartable_scaled
// expect: Scale the sum, not the table
int main() {
  math_opt::Model model;
  const dreamor::mathopt::VarTable<cI, cJ> x{model, IJ(), 0, 1};
  (void)dreamor::mathopt::sumExpr(IJ(), x * 100).build();
}
#endif  // DREAMOR_CASE_vartable_scaled

#ifdef DREAMOR_CASE_vartable_name_in_the_is_integer_place
// expect: A VarTable takes is_integer before the variables' name
int main() {
  math_opt::Model model;
  const dreamor::mathopt::VarTable<cI, cJ> x{model, IJ(), 0, 1, "x"};
  return static_cast<int>(x.size());
}
#endif  // DREAMOR_CASE_vartable_name_in_the_is_integer_place

#ifdef DREAMOR_CASE_vartable_null_name
// expect: A name is text, and nullptr is not one
int main() {
  math_opt::Model model;
  const dreamor::mathopt::VarTable<cI, cJ> x{model, IJ(), 0, 1, false, nullptr};
  return static_cast<int>(x.size());
}
#endif  // DREAMOR_CASE_vartable_null_name
