// SPDX-License-Identifier: Apache-2.0
// expect: Look a variable or value up by the table's key columns
#include "prelude.hpp"
struct Algo {
  IloEnv env;
  IloEnv getEnv() const { return env; }
  void getValues(IloNumArray, const IloNumVarArray) const {}
};
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ()};
  const auto plan = x.solution(Algo{env});
  (void)plan(2, 1);
}
