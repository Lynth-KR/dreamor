// SPDX-License-Identifier: Apache-2.0
#include "prelude.hpp"
int main() {
  IloEnv env;
  dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  x.add(cI{1}, cJ{1});
}
