// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: interval: closed at its integer type's largest value
#include <limits>
#include "prelude.hpp"

int main() try {
  // no value after the last one: the interval would hold every std::size_t, or none
  constexpr std::size_t kLargest = std::numeric_limits<std::size_t>::max();
  const auto sI                  = dreamor::makeSet<cI>(dreamor::interval::closed(std::size_t{0}, kLargest)).collect();
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
