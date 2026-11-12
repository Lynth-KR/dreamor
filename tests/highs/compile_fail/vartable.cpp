// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_vartable_scaled
// expect: Scale the sum, not the table
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  (void)(100 * x);
}
#endif  // DREAMOR_CASE_vartable_scaled

#ifdef DREAMOR_CASE_vartable_name_in_the_type_place
// expect: A VarTable takes the variables' type before their name
int main() {
  hs::HighsEnv env;
  const hs::VarTable<cI, cJ> x{env, IJ(), 0, 1, "x"};
  return static_cast<int>(x.size());
}
#endif  // DREAMOR_CASE_vartable_name_in_the_type_place

#ifdef DREAMOR_CASE_vartable_null_name
// expect: A name is text, and nullptr is not one
int main() {
  hs::HighsEnv env;
  const hs::VarTable<cI, cJ> x{env, IJ(), 0, 1, HighsVarType::kContinuous, nullptr};
  return static_cast<int>(x.size());
}
#endif  // DREAMOR_CASE_vartable_null_name

#ifdef DREAMOR_CASE_add_variable_null_name
// expect: A name is text, and nullptr is not one
int main() {
  hs::HighsEnv env;
  const hs::HighsVar x = env.addVariable(0, 1, HighsVarType::kContinuous, nullptr);
  return static_cast<int>(x.index());
}
#endif  // DREAMOR_CASE_add_variable_null_name

#ifdef DREAMOR_CASE_add_constr_null_name
// expect: A name is text, and nullptr is not one
int main() {
  hs::HighsEnv env;
  const hs::HighsVar x = env.addVariable();
  return static_cast<int>(hs::addConstr(env, x <= 1, nullptr).index());
}
#endif  // DREAMOR_CASE_add_constr_null_name
