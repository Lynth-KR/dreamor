// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: makeSet: source 2 holds one element twice, at 0 and 2
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> is{1, 2};
  const std::vector<std::size_t> js{7, 8, 7};
  return static_cast<int>(dreamor::makeSet<cI, cJ>(is, js).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
