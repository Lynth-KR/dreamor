// SPDX-License-Identifier: Apache-2.0
// expect: without braces
#include "prelude.hpp"
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.groupBy<cI, cJ>();
  (void)byIJ.slice({cI{2}, cJ{1}});
}
