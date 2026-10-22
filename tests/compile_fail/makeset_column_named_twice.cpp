// SPDX-License-Identifier: Apache-2.0
// expect: The same column type appears twice
#include "prelude.hpp"
int main() {
  const auto ij = IJ();
  (void)dreamor::makeSet<ICol>(ij, I).collect();
}
