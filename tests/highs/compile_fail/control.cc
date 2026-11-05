// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Must compile: proves the prelude and include paths work, so a "rejected" result below
// means the library rejected the code, not that the build setup is broken.
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  hs::addConstr(env, hs::sumExpr(sIJ, x).build() <= 1);
  const hs::HighsVar a = x(cI{1}, cJ{1});
  hs::setObjective(env, a * a + a);
  return x.size() == 4 ? 0 : 1;
}
