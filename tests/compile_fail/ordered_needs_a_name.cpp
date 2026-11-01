// SPDX-License-Identifier: Apache-2.0
// expect: Name the Ordered first
#include "prelude.hpp"
int main() {
  const auto sI = dreamor::makeSet<cI>(I).collect();
  (void)sI.ordered().prev(cI{1});
}
