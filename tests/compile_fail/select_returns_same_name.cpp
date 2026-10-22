// SPDX-License-Identifier: Apache-2.0
// expect: select(expr) returns two columns with the same name
#include "prelude.hpp"
int main() {
  (void)IJ().select([](ICol i) { return std::tuple<ICol, dreamor::Col<int, "i">>{i, 1}; });
}
