// SPDX-License-Identifier: Apache-2.0
// expect: Bind the set to a variable first
#include "prelude.hpp"
int main() {
  IloEnv env;
  (void)dreamor::concert::sumOf<ICol>(env, std::vector<std::size_t>{1, 2}, [&env](ICol i) {
    return IloExpr(env, i.get());
  }).build();
}
