// SPDX-License-Identifier: Apache-2.0
// expect: names a column these rows do not have
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumOf(env, sIJ, [&x](cI i, cK k) { return x(i, k.get()); }).build();
}
