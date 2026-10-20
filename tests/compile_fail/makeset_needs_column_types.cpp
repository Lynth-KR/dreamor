// SPDX-License-Identifier: Apache-2.0
// expect: Every column needs a name
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<std::size_t>(I).collect();
}
