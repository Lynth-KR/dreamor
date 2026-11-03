// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: interval: a value the column's integer type cannot hold
#include "prelude.hpp"

int main() try {
  const auto sI = dreamor::makeSet<cI>(dreamor::interval::closed(-1, 3)).collect();  // cI holds std::size_t
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
