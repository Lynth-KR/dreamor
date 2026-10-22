// SPDX-License-Identifier: Apache-2.0
// expect: A dependent column takes the same column twice
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<ICol, KCol>(I, [](ICol a, ICol b) {
    return std::vector<std::size_t>{a.get() + b.get()};
  }).collect();
}
