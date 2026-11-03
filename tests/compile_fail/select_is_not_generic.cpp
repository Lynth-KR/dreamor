// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Cannot read this callable's parameter list
#include "prelude.hpp"
int main() {
  (void)IJ().select([](auto i, auto j) { return std::tuple<cI, cJ>{j, i}; });
}
