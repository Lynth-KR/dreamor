// SPDX-License-Identifier: Apache-2.0
// expect: Slice by this groupBy's key columns
#include "prelude.hpp"
int main() {
  const auto sIJ = IJ();
  const auto byI = sIJ.groupBy<cI>();
  (void)byI.slice(cJ{1});
}
