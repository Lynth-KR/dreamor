// SPDX-License-Identifier: Apache-2.0
// expect: A VarTable is built on a set with exactly its key columns
#include "prelude.hpp"
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<JCol, ICol> x{env, IJ()};
}
