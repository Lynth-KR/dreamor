// SPDX-License-Identifier: Apache-2.0
// expect: select(expr) returns the same column twice
#include "prelude.hpp"
int main() {
  (void)IJ().select([](cI i, cJ j) { return std::tuple<cI, cI>{i, j}; });
}
