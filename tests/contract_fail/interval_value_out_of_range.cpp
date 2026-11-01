// SPDX-License-Identifier: Apache-2.0
// expect: interval: a value the column's integer type cannot hold
#include "prelude.hpp"

int main() try {
  const auto sI = dreamor::makeSet<cI>(dreamor::interval::closed(-1, 3)).collect();  // cI holds std::size_t
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
