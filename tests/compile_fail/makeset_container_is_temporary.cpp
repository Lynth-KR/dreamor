// SPDX-License-Identifier: Apache-2.0
// expect: A container source must be a named variable
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<cI>(std::vector<std::size_t>{1, 2}).collect();
}
