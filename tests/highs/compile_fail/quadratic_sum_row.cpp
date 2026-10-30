// SPDX-License-Identifier: Apache-2.0
// expect: HiGHS takes quadratic terms only in the objective
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  const auto square = [&x](ICol i, JCol j) { return x(i, j) * x(i, j); };
  hs::addConstr(env, hs::sumOf(sIJ, square).build() == x(ICol{1}, JCol{2}));
}
