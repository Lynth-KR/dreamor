// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A dependent source returning a reference into the model: checked once per range.
// expect: makeSet: dependent source 2 returned one element twice, at 0 and 1
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> is{0, 1};
  const std::vector<std::vector<std::size_t>> js{{3, 4}, {5, 5}};
  const auto js_of = [&js](cI i) -> const std::vector<std::size_t>& { return js[i.get()]; };
  return static_cast<int>(dreamor::makeSet<cI, cJ>(is, js_of).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
