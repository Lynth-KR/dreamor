// SPDX-License-Identifier: Apache-2.0
// expect: Scale the sum, not the table
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  (void)(100 * x);
}
