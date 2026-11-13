// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <format>
#include <tuple>

#ifdef DREAMOR_CASE_entity_vector_needs_primary_key
// expect: EntityVector<T> holds each entity once by its primary key
// when: `EntityVector<T>` for a `T` with no `primaryKey(const T&)`.
//
// fix: Define `primaryKey(const T&)` next to `T`, returning its id, code, or a `std::tuple` of them.
struct Unkeyed {
  std::size_t id;
};

const dreamor::EntityVector<Unkeyed> unkeyed;
#endif  // DREAMOR_CASE_entity_vector_needs_primary_key

#ifdef DREAMOR_CASE_entity_vector_key_reaches_itself
// expect: A primary key must not lead back to its own type
// when: A primary key that holds a `Ref` to its own type, such as a part keyed by its assembly, or that leads back to
// it through another entity's key - in an `EntityVector`, a name or `std::format`.
//
// fix: Key the entity by a value of its own, such as a code, and keep what it belongs to in a set, such as
// `IndexSet<cParent, cChild>`.
struct Part {  // keyed by its assembly, another Part
  dreamor::Ref<Part> assembly;
  std::size_t number;

  [[nodiscard]] auto primaryKey() const { return std::tuple{assembly, number}; }
};

const dreamor::EntityVector<Part> parts;
#endif  // DREAMOR_CASE_entity_vector_key_reaches_itself

#ifdef DREAMOR_CASE_entity_vector_key_reaches_itself_through_another
// expect: A primary key must not lead back to its own type
struct Shift;

struct Crew {  // keyed by a Shift, whose key holds a Crew
  dreamor::Ref<Shift> shift;
  std::size_t number;

  [[nodiscard]] auto primaryKey() const { return std::tuple{shift, number}; }
};

struct Shift {
  dreamor::Ref<Crew> lead;
  std::size_t day;

  [[nodiscard]] auto primaryKey() const { return std::tuple{lead, day}; }
};

const dreamor::EntityVector<Crew> crews;
#endif  // DREAMOR_CASE_entity_vector_key_reaches_itself_through_another

#ifdef DREAMOR_CASE_name_key_reaches_itself
// expect: A primary key must not lead back to its own type
struct Part {
  dreamor::Ref<Part> assembly;
  std::size_t number;

  [[nodiscard]] auto primaryKey() const { return std::tuple{assembly, number}; }
};

DREAMOR_ENTITY_COLUMN(cPart, Part);

[[maybe_unused]] std::string nameOf(const cPart& part) {
  return std::format("{}", part);
}
#endif  // DREAMOR_CASE_name_key_reaches_itself
