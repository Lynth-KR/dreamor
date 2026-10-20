// SPDX-License-Identifier: Apache-2.0
#include "prelude.hpp"
int main() {
  IloEnv env;
  dreamor::concert::VarTable<ICol, JCol> x{env, IJ()};
  x.add(ICol{1}, JCol{1});
}
