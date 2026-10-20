// SPDX-License-Identifier: Apache-2.0
// expect: Slice by this groupBy's key columns
#include "prelude.hpp"
int main() {
  const auto sIJ  = IJ();
  const auto byIJ = sIJ.groupBy<ICol, JCol>();
  (void)byIJ.slice(2, 1);
}
