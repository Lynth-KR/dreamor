// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
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
