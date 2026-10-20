// SPDX-License-Identifier: Apache-2.0
// expect: More column types than columns to name
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<ICol, JCol>(I).collect();
}
