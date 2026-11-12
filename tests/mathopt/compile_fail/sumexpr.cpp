// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_sumexpr_with_model
// expect: takes no model
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  (void)dreamor::mathopt::sumExpr(model, sIJ, x).build();
}
#endif  // DREAMOR_CASE_sumexpr_with_model

#ifdef DREAMOR_CASE_sumexpr_without_build
// expect: is not an expression yet
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  model.AddLinearConstraint(dreamor::mathopt::sumExpr(sIJ, x) <= 1);
}
#endif  // DREAMOR_CASE_sumexpr_without_build

#ifdef DREAMOR_CASE_sumexpr_without_build_assigned
// expect: UnbuiltSum
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  const math_opt::LinearExpression total = dreamor::mathopt::sumExpr(sIJ, x);
  (void)total;
}
#endif  // DREAMOR_CASE_sumexpr_without_build_assigned
