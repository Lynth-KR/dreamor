// SPDX-License-Identifier: Apache-2.0
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, 1, true, "x"};
}
