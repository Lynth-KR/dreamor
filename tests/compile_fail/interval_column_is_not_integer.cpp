// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: An interval fills an integer column
#include "prelude.hpp"
using cName = dreamor::Col<std::string, "name">;
int main() {
  (void)dreamor::makeSet<cName>(dreamor::interval::closed(0, 3)).collect();
}
