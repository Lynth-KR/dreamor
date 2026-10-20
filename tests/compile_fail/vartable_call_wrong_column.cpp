// SPDX-License-Identifier: Apache-2.0
// expect: Look a variable or value up by the table's key columns
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ()};
  (void)x(ICol{1}, KCol{1});
}
