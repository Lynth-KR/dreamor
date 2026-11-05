// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: makeSet: source 1 holds one row twice, at 0 and 1
#include "prelude.hpp"

int main() try {
  const std::vector<std::tuple<cI, cJ>> rows{{cI{1}, cJ{2}}, {cI{1}, cJ{2}}};
  return static_cast<int>(dreamor::makeSet<>(rows).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
