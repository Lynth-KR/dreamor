// SPDX-License-Identifier: Apache-2.0
// expect: takes no HighsEnv
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  (void)hs::sumOf(env, sIJ, x).build();
}
