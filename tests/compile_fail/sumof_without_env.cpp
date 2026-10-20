// SPDX-License-Identifier: Apache-2.0
// expect: takes the IloEnv first
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<ICol, JCol> x{env, sIJ};
  (void)dreamor::concert::sumOf(sIJ, [&x](ICol i, JCol j) { return x(i, j); }).build();
}
