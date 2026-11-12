// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Must compile: proves the prelude and include paths work, so a "rejected" result below
// means the library rejected the code, not that the build setup is broken.
#include "prelude.hpp"

int main() {
  math_opt::Model model;
  const auto sIJ = IJ();
  const dreamor::mathopt::VarTable<cI, cJ> x{model, sIJ, 0, 1};
  model.AddLinearConstraint(dreamor::mathopt::sumExpr(sIJ, x).build() <= 1);
  return x.size() == 4 ? 0 : 1;
}
