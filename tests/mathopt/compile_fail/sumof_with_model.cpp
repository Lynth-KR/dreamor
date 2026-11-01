// SPDX-License-Identifier: Apache-2.0
// expect: takes no model
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  (void)dreamor::mathopt::sumOf(model, sIJ, x).build();
}
