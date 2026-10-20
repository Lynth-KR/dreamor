// SPDX-License-Identifier: Apache-2.0
// expect: Take the columns themselves
#include "prelude.hpp"
int main() {
  IloEnv env;
  dreamor::concert::VarTable<ICol, JCol> x{env, IJ(), 0, [](std::size_t i) { return i; }};
}
