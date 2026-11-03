// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: EntityVector::modify: the entity is not one of this container's
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  entities.freeze();
  const Entity copy{1};  // the same key, another object
  entities.modify(&copy, [](Entity& e) { e.id = 1; });
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
