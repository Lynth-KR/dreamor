// SPDX-License-Identifier: Apache-2.0
// expect: is not an expression yet
#include <ilcplex/ilocplex.h>
#include "prelude.hpp"
int main() {
  IloEnv env;
  IloModel model(env);
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  model.add(dreamor::concert::sumOf(env, sIJ, [&x](cI i, cJ j) { return x(i, j); }) <= 1);
}
