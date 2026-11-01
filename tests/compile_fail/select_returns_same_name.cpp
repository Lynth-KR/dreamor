// SPDX-License-Identifier: Apache-2.0
// expect: select(expr) returns two columns with the same name
#include "prelude.hpp"
int main() {
  (void)IJ().select([](cI i) { return std::tuple<cI, dreamor::Col<int, "i">>{i, 1}; });
}
