// SPDX-License-Identifier: Apache-2.0
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  const auto y = x;
  (void)y;
}
