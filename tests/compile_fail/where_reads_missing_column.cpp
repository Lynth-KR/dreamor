// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: names a column this set does not have
#include "prelude.hpp"
int main() {
  (void)IJ().where([](cK k) { return k.get() > 1; });
}
