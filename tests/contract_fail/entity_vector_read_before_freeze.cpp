// SPDX-License-Identifier: Apache-2.0
// expect: EntityVector::iteration before freeze()
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  const auto sE = dreamor::makeSet<ECol>(entities).collect();  // freeze() forgotten
  return static_cast<int>(sE.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
