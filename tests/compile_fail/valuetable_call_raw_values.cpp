// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Look a variable or value up by the table's key columns
#include "prelude.hpp"
struct Algo {
  IloEnv env;
  IloEnv getEnv() const { return env; }
  void getValues(IloNumArray, const IloNumVarArray) const {}
};
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  const auto plan = x.solution(Algo{env});
  (void)plan(2, 1);
}
