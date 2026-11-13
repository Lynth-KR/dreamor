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
// when: `addConstr(env, x + y)`: an expression with no bounds.
//
// fix: `addConstr(env, x + y <= 1)`, or `addConstr(env, (0 <= x - y) <= 5)`.
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  hs::addConstr(env, hs::sumExpr(sIJ, x).build());
}
#endif  // DREAMOR_CASE_constr_without_bounds

#ifdef DREAMOR_CASE_objective_row
// expect: An objective is an expression, not a row
// when: `setObjective(env, x + y <= 1)`.
//
// fix: `setObjective(env, x + y)`.
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  (void)hs::minimize(env, hs::sumExpr(sIJ, x).build() <= 1);
}
#endif  // DREAMOR_CASE_objective_row

#ifdef DREAMOR_CASE_objective_of_another_backend
// expect: setObjective takes an expression of this backend's variables
// when: `setObjective(env, x)` with something that is not a HiGHS expression: a set, another backend's expression.
//
// fix: A `HighsVar`, a term, a `HighsLinearExpression` or a `HighsQuadraticExpression`.
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  hs::setObjective(env, sIJ);
}
#endif  // DREAMOR_CASE_objective_of_another_backend

#ifdef DREAMOR_CASE_quadratic_row
// expect: HiGHS takes quadratic terms only in the objective
// when: A quadratic expression in a row (`x * x <= 1`, a quadratic sum compared).
//
// fix: Put the square in `setObjective()`/`minimize()`.
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
