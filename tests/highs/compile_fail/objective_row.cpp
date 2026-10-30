// SPDX-License-Identifier: Apache-2.0
// expect: An objective is an expression, not a row
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  (void)hs::minimize(env, hs::sumOf(sIJ, x).build() <= 1);
}
