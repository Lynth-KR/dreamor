// SPDX-License-Identifier: Apache-2.0
// expect: Cannot read this predicate's parameter list
#include "prelude.hpp"
int main() {
  (void)IJ().where([](auto) { return true; });
}
