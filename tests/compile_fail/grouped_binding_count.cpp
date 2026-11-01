// SPDX-License-Identifier: Apache-2.0
#include "prelude.hpp"
int main() {
  for (const auto& [key, rows] : IJ().groupBy<cI, cJ>()) {
    (void)rows;
  }
}
