// SPDX-License-Identifier: Apache-2.0
// expect: EntityVector::emplace after freeze()
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  entities.freeze();
  entities.emplace(Entity{2});  // the model may hold its elements already
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
