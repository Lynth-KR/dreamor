// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: A dependent column cannot be a bare function name or a std::function
#include <functional>
#include "prelude.hpp"
int main() {
  const std::function<std::vector<std::size_t>(cI)> next = [](cI i) { return std::vector<std::size_t>{i.get() + 1}; };
  (void)dreamor::makeSet<cI, cJ>(I, next).build();
}
