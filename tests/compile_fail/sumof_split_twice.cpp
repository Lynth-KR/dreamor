// SPDX-License-Identifier: Apache-2.0
// expect: split() once
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<ICol, JCol> x{env, sIJ};
  (void)dreamor::concert::sumOf(env, sIJ, [&x](ICol i, JCol j) { return x(i, j); })
      .split([](ICol) { return true; })
      .split([](JCol) { return true; })
      .build();
}
