// SPDX-License-Identifier: Apache-2.0
// expect: An interval fills an integer column
#include "prelude.hpp"
using cName = dreamor::Col<std::string, "name">;
int main() {
  (void)dreamor::makeSet<cName>(dreamor::interval::closed(0, 3)).collect();
}
