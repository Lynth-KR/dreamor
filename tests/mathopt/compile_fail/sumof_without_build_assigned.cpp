// SPDX-License-Identifier: Apache-2.0
// expect: UnbuiltSum
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<ICol, JCol> x{model, sIJ, 0, 1};
  const math_opt::LinearExpression total = dreamor::mathopt::sumOf(sIJ, x);
  (void)total;
}
