// SPDX-License-Identifier: Apache-2.0
// expect: names a column this set does not have
#include "prelude.hpp"
int main() {
  (void)IJ().where([](KCol k) { return k.get() > 1; });
}
