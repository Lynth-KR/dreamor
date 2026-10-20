// SPDX-License-Identifier: Apache-2.0
// expect: Take the columns themselves
#include "prelude.hpp"
int main() {
  (void)IJ().where([](std::size_t i) { return i > 1; });
}
