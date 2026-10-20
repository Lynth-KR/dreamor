// SPDX-License-Identifier: Apache-2.0
// expect: names a column these rows do not have
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, [](KCol k) { return k.get(); }};
}
