// SPDX-License-Identifier: Apache-2.0
// Must compile: proves the prelude and include paths work, so a "rejected" result below
// means the library rejected the code, not that the build setup is broken.
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, 1};
  return x.size() == 4 ? 0 : 1;
}
