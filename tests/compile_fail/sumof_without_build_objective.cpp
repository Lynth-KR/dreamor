// SPDX-License-Identifier: Apache-2.0
// expect: UnbuiltSum
#include <ilcplex/ilocplex.h>
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<ICol, JCol> x{env, sIJ};
  IloModel model(env);
  model.add(IloMinimize(env, dreamor::concert::sumOf(env, sIJ, x)));
}
