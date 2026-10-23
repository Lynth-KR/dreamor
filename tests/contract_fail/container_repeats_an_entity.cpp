// SPDX-License-Identifier: Apache-2.0
// expect: makeSet: source 1 holds one element twice, at 1 and 2
#include "prelude.hpp"

int main() try {
  const Entity a{0};
  const Entity b{1};
  const std::vector<const Entity*> es{&a, &b, &b};
  return static_cast<int>(dreamor::makeSet<ECol>(es).collect().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
