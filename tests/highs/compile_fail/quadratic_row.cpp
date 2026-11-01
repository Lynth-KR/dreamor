// SPDX-License-Identifier: Apache-2.0
// expect: HiGHS takes quadratic terms only in the objective
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  const hs::HighsVar a = x(cI{1}, cJ{1});
  hs::addConstr(env, a * a <= 1);
}
