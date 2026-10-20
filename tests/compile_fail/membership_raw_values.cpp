// SPDX-License-Identifier: Apache-2.0
// expect: Ask membership
#include "prelude.hpp"
int main() {
  const auto sIJ   = IJ();
  const auto in_ij = sIJ.membership();
  (void)in_ij(1, 2);
}
