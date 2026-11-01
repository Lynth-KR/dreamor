// SPDX-License-Identifier: Apache-2.0
// expect: is not an expression yet
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  model.AddLinearConstraint(dreamor::mathopt::sumOf(sIJ, x) <= 1);
}
