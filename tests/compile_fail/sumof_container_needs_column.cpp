// SPDX-License-Identifier: Apache-2.0
// expect: sumOf takes an index set
#include "prelude.hpp"
int main() {
  IloEnv env;
  (void)dreamor::concert::sumOf(env, I, [&env](ICol i) { return IloExpr(env, i.get()); }).build();
}
