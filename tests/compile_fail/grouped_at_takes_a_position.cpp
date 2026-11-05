// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: take a group's position, 0 to size() - 1, not a column
#include "prelude.hpp"
int main() {
  const auto sIJ = IJ();
  const auto byI = sIJ.groupBy<cI>();
  (void)byI.at(cI{1});
}
