// SPDX-License-Identifier: Apache-2.0
// expect: select(expr) returns the new row's columns
#include "prelude.hpp"
int main() {
  (void)IJ().select([](cI i, cJ j) { return std::tuple{j.get(), i.get()}; });
}
