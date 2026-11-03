// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: EntityVector::modify: fn changed the entity's primary key
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  const auto [first, inserted] = entities.emplace(Entity{1});
  entities.freeze();
  entities.modify(first, [](Entity& e) { e.id = 2; });
  return inserted ? 0 : 2;
} catch (...) {
  return 1;  // no message: the runner reports it
}
