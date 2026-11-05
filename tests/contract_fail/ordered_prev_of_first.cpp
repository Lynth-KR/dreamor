// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Ordered::prevOf has no row to return
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sI       = dreamor::makeSet<cI>(values).build();
  const auto orderedI = sI.ordered();
  return static_cast<int>(std::get<cI>(orderedI.prevOf(cI{1})).get());  // 1 is the first: nothing comes before it
} catch (...) {
  return 1;  // no message: the runner reports it
}
