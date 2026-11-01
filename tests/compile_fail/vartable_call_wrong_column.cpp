// SPDX-License-Identifier: Apache-2.0
// expect: Look a variable or value up by the table's key columns
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  (void)x(cI{1}, cK{1});
}
