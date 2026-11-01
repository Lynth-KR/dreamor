// SPDX-License-Identifier: Apache-2.0
// expect: An interval counts integers
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<cI>(dreamor::interval::closed(0.0, 3.0)).collect();
}
