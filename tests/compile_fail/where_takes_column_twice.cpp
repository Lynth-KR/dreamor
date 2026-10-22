// SPDX-License-Identifier: Apache-2.0
// expect: This callable takes the same column twice
#include "prelude.hpp"
int main() {
  (void)IJ().where([](ICol a, ICol b) { return a.get() < b.get(); });
}
