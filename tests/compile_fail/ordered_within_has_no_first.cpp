// SPDX-License-Identifier: Apache-2.0
// expect: ordered<Columns...>() orders within each group
#include "prelude.hpp"
int main() {
  const auto byI = IJ().ordered<cI>();
  static_cast<void>(byI.first());
}
