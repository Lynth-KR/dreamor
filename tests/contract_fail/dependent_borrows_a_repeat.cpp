// SPDX-License-Identifier: Apache-2.0
// A dependent source returning a reference into the model: checked once per range.
// expect: makeSet: dependent source 2 returned one element twice, at 0 and 1
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> is{0, 1};
  const std::vector<std::vector<std::size_t>> js{{3, 4}, {5, 5}};
  const auto js_of = [&js](ICol i) -> const std::vector<std::size_t>& { return js[i.get()]; };
  return static_cast<int>(dreamor::makeSet<ICol, JCol>(is, js_of).collect().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
