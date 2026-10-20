// SPDX-License-Identifier: Apache-2.0
// expect: Cannot read this callable's parameter list
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<ICol, JCol> x{env, sIJ};
  (void)dreamor::concert::sumOf(env, sIJ, [&x](auto i, auto j) { return x(i, j); }).build();
}
