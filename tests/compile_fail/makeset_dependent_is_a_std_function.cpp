// SPDX-License-Identifier: Apache-2.0
// expect: A dependent column cannot be a bare function name or a std::function
#include <functional>
#include "prelude.hpp"
int main() {
  const std::function<std::vector<std::size_t>(cI)> next = [](cI i) { return std::vector<std::size_t>{i.get() + 1}; };
  (void)dreamor::makeSet<cI, cJ>(I, next).collect();
}
