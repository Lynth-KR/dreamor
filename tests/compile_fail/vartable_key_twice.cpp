// SPDX-License-Identifier: Apache-2.0
// expect: names one key column twice
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, ICol> x{env, IJ(), 0, 1};
}
