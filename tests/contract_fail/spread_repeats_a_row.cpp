// SPDX-License-Identifier: Apache-2.0
// expect: makeSet: source 1 holds one row twice, at 0 and 1
#include "prelude.hpp"

int main() try {
  const std::vector<std::tuple<ICol, JCol>> rows{{ICol{1}, JCol{2}}, {ICol{1}, JCol{2}}};
  return static_cast<int>(dreamor::makeSet<>(rows).collect().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
