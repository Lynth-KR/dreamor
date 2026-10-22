// SPDX-License-Identifier: Apache-2.0
// expect: Cannot read this callable's parameter list
#include "prelude.hpp"
int main() {
  (void)IJ().select([](auto i, auto j) { return std::tuple<ICol, JCol>{j, i}; });
}
