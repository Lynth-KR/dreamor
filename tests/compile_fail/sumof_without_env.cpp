// SPDX-License-Identifier: Apache-2.0
// expect: takes the IloEnv first
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumOf(sIJ, [&x](cI i, cJ j) { return x(i, j); }).build();
}
