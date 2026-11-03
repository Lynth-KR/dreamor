// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: sumOf takes an index set
#include "prelude.hpp"
int main() {
  IloEnv env;
  (void)dreamor::concert::sumOf(env, I, [&env](cI i) { return IloExpr(env, i.get()); }).build();
}
