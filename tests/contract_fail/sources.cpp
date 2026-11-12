// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_container_holds_an_empty_ref
// expect: building a set: source 1 holds an empty Ref at 1
// contract: source_holds_no_empty_ref
int main() try {
  dreamor::EntityVector<Entity> entities;
  static_cast<void>(entities.emplace(Entity{0}));
  entities.freeze();
  const std::vector<dreamor::Ref<Entity>> es{entities.at(0), dreamor::Ref<Entity>{}};
  return static_cast<int>(dreamor::index_set::relational::from<cE>(es).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_container_holds_an_empty_ref

#ifdef DREAMOR_CASE_container_repeats_a_value
// expect: building a set: source 2 holds one element twice, at 0 and 2
// contract: source_holds_no_repeat
int main() try {
  const std::vector<std::size_t> is{1, 2};
  const std::vector<std::size_t> js{7, 8, 7};
  return static_cast<int>(dreamor::index_set::relational::from<cI>(is).cross<cJ>(js).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_container_repeats_a_value

#ifdef DREAMOR_CASE_container_repeats_an_entity
// expect: building a set: source 1 holds one element twice, at 1 and 2
// contract: source_holds_no_repeat
int main() try {
  dreamor::EntityVector<Entity> entities;
  static_cast<void>(entities.emplace(Entity{0}));
  static_cast<void>(entities.emplace(Entity{1}));
  entities.freeze();
  const std::vector<dreamor::Ref<Entity>> es{entities.at(0), entities.at(1), entities.at(1)};
  return static_cast<int>(dreamor::index_set::relational::from<cE>(es).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_container_repeats_an_entity

#ifdef DREAMOR_CASE_dependent_borrows_a_repeat
// expect: building a set: lateral source 2 returned one element twice, at 0 and 1
// contract: source_holds_no_repeat
// A lateral source returning a reference into the model: checked once per range.

int main() try {
  const std::vector<std::size_t> is{0, 1};
  const std::vector<std::vector<std::size_t>> js{{3, 4}, {5, 5}};
  const auto js_of = [&js](cI i) -> const std::vector<std::size_t>& { return js[i.get()]; };
  return static_cast<int>(dreamor::index_set::relational::from<cI>(is).lateral<cJ>(js_of).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_dependent_borrows_a_repeat

#ifdef DREAMOR_CASE_dependent_returns_a_repeat
// expect: building a set: lateral source 2 returned one element twice, at 0 and 1
// contract: source_holds_no_repeat
// A lateral source returning a fresh vector: checked on every call.

int main() try {
  const std::vector<std::size_t> is{1, 2};
  const auto js_of = [](cI i) { return std::vector<std::size_t>{i.get(), i.get()}; };
  return static_cast<int>(dreamor::index_set::relational::from<cI>(is).lateral<cJ>(js_of).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_dependent_returns_a_repeat

#ifdef DREAMOR_CASE_dependent_returns_an_empty_ref
// expect: building a set: lateral source 2 returned an empty Ref at 0
// contract: source_holds_no_empty_ref
int main() try {
  dreamor::EntityVector<Entity> entities;
  static_cast<void>(entities.emplace(Entity{0}));
  entities.freeze();
  const std::vector<std::size_t> is{1};
  const auto none_of = [](cI) { return std::vector<dreamor::Ref<Entity>>{dreamor::Ref<Entity>{}}; };
  return static_cast<int>(dreamor::index_set::relational::from<cI>(is).lateral<cE>(none_of).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_dependent_returns_an_empty_ref

#ifdef DREAMOR_CASE_indexset_records_give_an_empty_ref
// expect: IndexSet{records, row_of}: row_of returned an empty Ref for record 1
// contract: source_holds_no_empty_ref
int main() try {
  dreamor::EntityVector<Entity> entities;
  static_cast<void>(entities.emplace(Entity{0}));
  entities.freeze();
  const std::vector<std::size_t> records{0, 1};
  const auto row_of = [&entities](std::size_t id) { return std::tuple<cE, cI>{entities.find(id), id}; };
  const dreamor::IndexSet<cE, cI> sEI{records, row_of};
  return static_cast<int>(sEI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_indexset_records_give_an_empty_ref

#ifdef DREAMOR_CASE_indexset_records_give_one_row_twice
// expect: records 0 and 2 (counting from 0) both give the row
// contract: source_holds_no_repeat
int main() try {
  const std::vector<std::size_t> records{4, 7, 4};
  const auto row_of = [](std::size_t i) { return cI{i}; };
  const dreamor::IndexSet<cI> sI{records, row_of};
  return static_cast<int>(sI.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_indexset_records_give_one_row_twice

#ifdef DREAMOR_CASE_spread_holds_an_empty_ref
// expect: building a set: source 1 holds a row with an empty Ref at 1
// contract: source_holds_no_empty_ref
int main() try {
  dreamor::EntityVector<Entity> entities;
  static_cast<void>(entities.emplace(Entity{0}));
  entities.freeze();
  const std::vector<std::tuple<cE, cI>> rows{{entities.at(0), cI{1}}, {dreamor::Ref<Entity>{}, cI{2}}};
  return static_cast<int>(dreamor::index_set::relational::from(rows).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_spread_holds_an_empty_ref

#ifdef DREAMOR_CASE_spread_repeats_a_row
// expect: building a set: source 1 holds one row twice, at 0 and 1
// contract: source_holds_no_repeat
int main() try {
  const std::vector<std::tuple<cI, cJ>> rows{{cI{1}, cJ{2}}, {cI{1}, cJ{2}}};
  return static_cast<int>(dreamor::index_set::relational::from(rows).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_spread_repeats_a_row
