// SPDX-License-Identifier: Apache-2.0
// Must compile: proves the prelude and include paths work, so a "rejected" result below
// means the library rejected the code, not that the build setup is broken.
#include "prelude.hpp"
int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<ICol, JCol> x{model, sIJ, 0, 1};
  model.AddLinearConstraint(dreamor::mathopt::sumOf(sIJ, x).build() <= 1);
  return x.size() == 4 ? 0 : 1;
}
