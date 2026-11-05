// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: ordered() takes no columns
#include "prelude.hpp"
int main() {
  const auto sIJ = IJ();
  (void)sIJ.ordered<cI>();
}
