// SPDX-License-Identifier: Apache-2.0
// expect: Look a variable or value up by the table's key columns
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<ICol, JCol> x{model, sIJ, 0, 1};
  (void)x(2, 1);
}
