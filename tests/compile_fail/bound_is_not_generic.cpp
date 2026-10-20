// SPDX-License-Identifier: Apache-2.0
// expect: Cannot read this callable's parameter list
#include "prelude.hpp"
int main() {
  IloEnv env;
  dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, [](auto i) { return i.get(); }};
}
