// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: first(m) and last(m) name a group's ends
#include "prelude.hpp"
int main() {
  const auto all = IJ().ordered();
  static_cast<void>(all.first(cI{1}));
}
