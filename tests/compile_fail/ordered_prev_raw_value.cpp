// SPDX-License-Identifier: Apache-2.0
// expect: Name an element of the ordered set
#include "prelude.hpp"
int main() {
  const auto sI       = dreamor::makeSet<ICol>(I).collect();
  const auto orderedI = sI.ordered();
  (void)orderedI.prev(1);
}
