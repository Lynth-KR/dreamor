// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: A container source must be a named variable
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<cI>(std::vector<std::size_t>{1, 2}).collect();
}
