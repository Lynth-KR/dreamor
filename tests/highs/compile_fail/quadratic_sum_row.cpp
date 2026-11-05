// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: HiGHS takes quadratic terms only in the objective
#include "prelude.hpp"
int main() {
  hs::HighsEnv env;
  const auto sIJ = IJ();
  const hs::VarTable<cI, cJ> x{env, sIJ, 0, 1};
  const auto square = [&x](cI i, cJ j) { return x(i, j) * x(i, j); };
  hs::addConstr(env, hs::sumExpr(sIJ, square).build() == x(cI{1}, cJ{2}));
}
