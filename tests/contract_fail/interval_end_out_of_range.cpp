// SPDX-License-Identifier: Apache-2.0
// expect: interval: an end its interval's integer type cannot hold
#include "prelude.hpp"

int main() try {
  const auto sI = dreamor::makeSet<cI>(dreamor::interval::closed(-1, 3U)).collect();  // the ends meet in unsigned
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
