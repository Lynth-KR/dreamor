// SPDX-License-Identifier: Apache-2.0
// expect: A dependent column cannot be a bare function name or a std::function
#include "prelude.hpp"
std::vector<std::size_t> next(cI i) {
  return {i.get() + 1};
}
int main() {
  (void)dreamor::makeSet<cI, cJ>(I, next).collect();
}
