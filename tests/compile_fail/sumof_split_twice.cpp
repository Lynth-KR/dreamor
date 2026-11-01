// SPDX-License-Identifier: Apache-2.0
// expect: split() once
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumOf(env, sIJ, [&x](cI i, cJ j) { return x(i, j); })
      .split([](cI) { return true; })
      .split([](cJ) { return true; })
      .build();
}
