// SPDX-License-Identifier: Apache-2.0
// expect: makeSet: source 2 holds one element twice, at 0 and 2
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> is{1, 2};
  const std::vector<std::size_t> js{7, 8, 7};
  return static_cast<int>(dreamor::makeSet<ICol, JCol>(is, js).collect().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
