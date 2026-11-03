// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: EntityVector::iteration before freeze()
#include "prelude.hpp"

int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  const auto sE = dreamor::makeSet<cE>(entities).collect();  // freeze() forgotten
  return static_cast<int>(sE.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
