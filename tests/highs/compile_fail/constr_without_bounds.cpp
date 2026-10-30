// SPDX-License-Identifier: Apache-2.0
// expect: addConstr takes a row
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  hs::addConstr(env, hs::sumOf(sIJ, x).build());
}
