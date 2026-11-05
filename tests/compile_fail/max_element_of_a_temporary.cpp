// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: hand back a row by its address: name the set first
#include "prelude.hpp"
int main() {
  const auto big   = [](cI i) { return i.get() > 1; };
  const auto value = [](cJ j) { return j.get(); };
  (void)dreamor::maxElement(IJ().where(big), value);
}
