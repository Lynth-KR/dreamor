// SPDX-License-Identifier: Apache-2.0
// expect: No column of this set has that name
#include "prelude.hpp"
int main() {
  (void)IJ().groupBy<KCol>();
}
