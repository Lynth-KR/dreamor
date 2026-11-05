// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Bind the set to a variable first
#include "prelude.hpp"
int main() {
  IloEnv env;
  (void)dreamor::concert::sumExpr<cI>(env, std::vector<std::size_t>{1, 2}, [&env](cI i) {
    return IloExpr(env, i.get());
  }).build();
}
