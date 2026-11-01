// SPDX-License-Identifier: Apache-2.0
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
