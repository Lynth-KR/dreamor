// SPDX-License-Identifier: Apache-2.0
// expect: select(expr) returns the same column twice
#include "prelude.hpp"
int main() {
  (void)IJ().select([](ICol i, JCol j) { return std::tuple<ICol, ICol>{i, j}; });
}
