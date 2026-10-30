// SPDX-License-Identifier: Apache-2.0
// Must compile: proves the prelude and include paths work, so a "rejected" result below
// means the library rejected the code, not that the build setup is broken.
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  hs::addConstr(env, hs::sumOf(sIJ, x).build() <= 1);
  const hs::HighsVar a = x(ICol{1}, JCol{1});
  hs::setObjective(env, a * a + a);
  return x.size() == 4 ? 0 : 1;
}
