// SPDX-License-Identifier: Apache-2.0
// expect: HiGHS takes quadratic terms only in the objective
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<ICol, JCol> x{env, sIJ, 0, 1};
  const hs::HighsVar a = x(ICol{1}, JCol{1});
  hs::addConstr(env, a * a <= 1);
}
