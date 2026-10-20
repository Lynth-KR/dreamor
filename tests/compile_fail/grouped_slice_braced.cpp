// SPDX-License-Identifier: Apache-2.0
// expect: without braces
#include "prelude.hpp"
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.groupBy<ICol, JCol>();
  (void)byIJ.slice({ICol{2}, JCol{1}});
}
