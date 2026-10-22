// SPDX-License-Identifier: Apache-2.0
// expect: This callable takes the same column twice
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, 1};
  (void)dreamor::concert::sumOf(env, IJ(), [&x](ICol a, ICol b) { return x(a, JCol{b.get()}); }).build();
}
