// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: select(expr) returns the new row's columns
#include "prelude.hpp"
int main() {
  (void)IJ().select([](cI i, cJ j) { return std::tuple{j.get(), i.get()}; });
}
