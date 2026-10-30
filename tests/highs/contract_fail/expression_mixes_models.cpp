// SPDX-License-Identifier: Apache-2.0
// expect: one expression holds variables of two HighsEnvs
#include "prelude.hpp"

int main() try {
  hs::HighsEnv first;
  hs::HighsEnv second;
  const auto set = sI();
  const hs::VarTable<ICol> x{first, set, 0, 1};
  const hs::VarTable<ICol> y{second, set, 0, 1};
  const hs::HighsLinearExpression mixed = x(ICol{1}) + y(ICol{1});
  return static_cast<int>(mixed.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
