// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_entity_vector_emplace_after_freeze
// expect: EntityVector::emplace after freeze()
// contract: entity_vector_filling
int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  entities.freeze();
  entities.emplace(Entity{2});  // the model may hold its elements already
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_entity_vector_emplace_after_freeze

#ifdef DREAMOR_CASE_entity_vector_read_before_freeze
// expect: EntityVector::iteration before freeze()
// contract: entity_vector_reading
int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  const auto sE = dreamor::index_set::relational::from<cE>(entities).build();  // freeze() forgotten
  return static_cast<int>(sE.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_entity_vector_read_before_freeze

#ifdef DREAMOR_CASE_entity_vector_ref_of_a_copy
// expect: EntityVector::ref of an entity that is not one of its elements
// contract: entity_vector_element
int main() try {
  dreamor::EntityVector<Entity> entities;
  entities.emplace(Entity{1});
  entities.freeze();
  const Entity copy = entities[0];  // a copy, as a loop by value makes one
  return static_cast<int>(entities.ref(copy)->id);
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_entity_vector_ref_of_a_copy

#ifdef DREAMOR_CASE_ref_read_empty
// expect: Ref: an empty Ref read - it refers to no entity
// contract: ref_not_empty
int main() try {
  const dreamor::Ref<Entity> none;  // find of a key not there gives one
  return static_cast<int>(none->id);
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_ref_read_empty
