// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: A dependent column takes the same column twice
#include "prelude.hpp"
int main() {
  (void)dreamor::makeSet<cI, cK>(I, [](cI a, cI b) { return std::vector<std::size_t>{a.get() + b.get()}; }).build();
}
