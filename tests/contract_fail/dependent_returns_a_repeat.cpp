// SPDX-License-Identifier: Apache-2.0
// A dependent source returning a fresh vector: checked on every call.
// expect: makeSet: dependent source 2 returned one element twice, at 0 and 1
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> is{1, 2};
  const auto js_of = [](ICol i) { return std::vector<std::size_t>{i.get(), i.get()}; };
  return static_cast<int>(dreamor::makeSet<ICol, JCol>(is, js_of).collect().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
