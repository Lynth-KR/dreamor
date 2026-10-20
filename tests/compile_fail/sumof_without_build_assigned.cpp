// SPDX-License-Identifier: Apache-2.0
// expect: UnbuiltSum
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<ICol, JCol> x{env, sIJ};
  IloExpr total = dreamor::concert::sumOf(env, sIJ, x);
  (void)total;
}
