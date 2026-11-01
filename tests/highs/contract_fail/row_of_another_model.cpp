// SPDX-License-Identifier: Apache-2.0
// expect: the expression's variables belong to another HighsEnv
#include "prelude.hpp"

int main() try {
  hs::HighsEnv first;
  hs::HighsEnv second;
  const auto set = sI();
  const hs::VarTable<cI> x{first, set, 0, 1};
  return hs::addConstr(second, x(cI{1}) <= 1).index();
} catch (...) {
  return 1;  // no message: the runner reports it
}
