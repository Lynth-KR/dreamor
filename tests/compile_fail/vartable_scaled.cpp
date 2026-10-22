// SPDX-License-Identifier: Apache-2.0
// expect: Scale the sum, not the table
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, 1};
  (void)dreamor::concert::sumOf(env, IJ(), 100 * x).build();
}
