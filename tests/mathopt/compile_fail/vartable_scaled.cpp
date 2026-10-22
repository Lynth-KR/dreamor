// SPDX-License-Identifier: Apache-2.0
// expect: Scale the sum, not the table
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const dreamor::mathopt::VarTable<ICol, JCol> x{model, IJ(), 0, 1};
  (void)dreamor::mathopt::sumOf(IJ(), x * 100).build();
}
