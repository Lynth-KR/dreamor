// SPDX-License-Identifier: Apache-2.0
// expect: EntityVector::modify: the entity is not one of this container's
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  const Entity copy{1};  // the same key, another object
  entities.modify(&copy, [](Entity& e) { e.id = 1; });
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
