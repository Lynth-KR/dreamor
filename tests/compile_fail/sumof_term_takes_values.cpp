// SPDX-License-Identifier: Apache-2.0
// expect: Take the columns themselves
#include "prelude.hpp"
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<ICol, JCol> x{env, sIJ};
  (void)dreamor::concert::sumOf(env, sIJ, [&x](std::size_t i, std::size_t j) { return x(i, j); }).build();
}
