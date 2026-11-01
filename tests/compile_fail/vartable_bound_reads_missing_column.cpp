// SPDX-License-Identifier: Apache-2.0
// expect: names a column these rows do not have
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, [](cK k) { return k.get(); }};
}
