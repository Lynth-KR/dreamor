// SPDX-License-Identifier: Apache-2.0
// expect: Name the Grouped first
#include "prelude.hpp"
int main() {
  (void)IJ().groupBy<ICol>().slice(ICol{1});
}
