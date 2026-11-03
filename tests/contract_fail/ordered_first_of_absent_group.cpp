// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: Ordered::first has no row to return
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sII     = dreamor::makeSet<cI, cJ>(values, values).collect();
  const auto withinI = sII.ordered<cI>();
  return static_cast<int>(std::get<cJ>(withinI.first(cI{4})).get());  // no row has i = 4: the group is not there
} catch (...) {
  return 1;  // no message: the runner reports it
}
