// SPDX-License-Identifier: Apache-2.0
// expect: EntityVector::modify: fn changed the entity's primary key
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  const auto [first, inserted] = entities.emplace(Entity{1});
  entities.modify(first, [](Entity& e) { e.id = 2; });
  return inserted ? 0 : 2;
} catch (...) {
  return 1;  // no message: the runner reports it
}
