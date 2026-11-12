// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_constr_without_bounds
// expect: addConstr takes a row
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  hs::addConstr(env, hs::sumExpr(sIJ, x).build());
}
#endif  // DREAMOR_CASE_constr_without_bounds

#ifdef DREAMOR_CASE_objective_row
// expect: An objective is an expression, not a row
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  (void)hs::minimize(env, hs::sumExpr(sIJ, x).build() <= 1);
}
#endif  // DREAMOR_CASE_objective_row

#ifdef DREAMOR_CASE_objective_of_another_backend
// expect: setObjective takes an expression of this backend's variables
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  hs::setObjective(env, sIJ);
}
#endif  // DREAMOR_CASE_objective_of_another_backend

#ifdef DREAMOR_CASE_quadratic_row
// expect: HiGHS takes quadratic terms only in the objective
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  const hs::HighsVar a = x(cI{1}, cJ{1});
  hs::addConstr(env, a * a <= 1);
}
#endif  // DREAMOR_CASE_quadratic_row

#ifdef DREAMOR_CASE_quadratic_sum_row
// expect: HiGHS takes quadratic terms only in the objective
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  const auto square = [&x](cI i, cJ j) { return x(i, j) * x(i, j); };
  hs::addConstr(env, hs::sumExpr(sIJ, square).build() == x(cI{1}, cJ{2}));
}
#endif  // DREAMOR_CASE_quadratic_sum_row
