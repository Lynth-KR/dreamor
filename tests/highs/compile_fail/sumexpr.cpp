// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_sumexpr_with_highs
// expect: takes no HighsEnv
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  (void)hs::sumExpr(env, sIJ, x).build();
}
#endif  // DREAMOR_CASE_sumexpr_with_highs

#ifdef DREAMOR_CASE_sumexpr_without_build
// expect: is not an expression yet
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  hs::addConstr(env, hs::sumExpr(sIJ, x) <= 1);
}
#endif  // DREAMOR_CASE_sumexpr_without_build

#ifdef DREAMOR_CASE_minimize_sumexpr_without_build
// expect: is not an expression yet
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  (void)hs::minimize(env, hs::sumExpr(sIJ, x));
}
#endif  // DREAMOR_CASE_minimize_sumexpr_without_build

#ifdef DREAMOR_CASE_set_objective_sumexpr_without_build
// expect: is not an expression yet
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  hs::setObjective(env, hs::sumExpr(sIJ, x), ObjSense::kMaximize);
}
#endif  // DREAMOR_CASE_set_objective_sumexpr_without_build
