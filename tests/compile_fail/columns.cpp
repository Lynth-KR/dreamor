// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "dreamor/format_buffer.hpp"

#ifdef DREAMOR_CASE_column_holds_a_pointer
// expect: A column refers to an entity by a Ref, not a pointer
struct Machine {
  std::size_t id;
};

DREAMOR_VALUE_COLUMN(cMachine, const Machine*);

int main() {
  const Machine m1{1};
  const cMachine m{&m1};
  return static_cast<int>(m->id);
}
#endif  // DREAMOR_CASE_column_holds_a_pointer

#ifdef DREAMOR_CASE_column_holds_a_value_without_equality
// expect: A column identifies its rows by ==: it holds a Ref, an integer
struct Blob {
  std::size_t size;
};

DREAMOR_VALUE_COLUMN(cBlob, Blob);

int main() {
  const cBlob blob{Blob{1}};
  return static_cast<int>(blob.get().size);
}
#endif  // DREAMOR_CASE_column_holds_a_value_without_equality

#ifdef DREAMOR_CASE_column_holds_a_value_without_hash
// expect: a type of yours needs std::hash as well as ==
struct Grade {
  std::size_t rank;
  friend bool operator==(const Grade&, const Grade&) = default;
};

DREAMOR_VALUE_COLUMN(cGrade, Grade);

int main() {
  const cGrade grade{Grade{1}};
  return static_cast<int>(grade.get().rank);
}
#endif  // DREAMOR_CASE_column_holds_a_value_without_hash

#ifdef DREAMOR_CASE_column_holds_a_double
// expect: which a floating-point number does not keep
DREAMOR_VALUE_COLUMN(cWeight, double);

int main() {
  const cWeight weight{1.5};
  return weight > 1.0 ? 0 : 1;
}
#endif  // DREAMOR_CASE_column_holds_a_double

#ifdef DREAMOR_CASE_column_holds_a_wchar_t
// expect: A column holds a char, not a wchar_t
DREAMOR_VALUE_COLUMN(cWide, wchar_t);

int main() {
  const cWide wide{L'a'};
  return static_cast<int>(wide.get());
}
#endif  // DREAMOR_CASE_column_holds_a_wchar_t

#ifdef DREAMOR_CASE_ref_column_from_a_returned_vector
// expect: A Ref column takes its entities from their EntityVector
struct Thing {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cThing, Thing);

int main() {
  const auto things_of = [](cI i) {
    dreamor::EntityVector<Thing> made;  // a new vector each time, gone when the binding moves on
    static_cast<void>(made.emplace(Thing{i.get()}));
    made.freeze();
    return made;
  };
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cThing>(things_of).build();
}
#endif  // DREAMOR_CASE_ref_column_from_a_returned_vector

#ifdef DREAMOR_CASE_ref_column_from_a_vector
// expect: A Ref column takes its entities from their EntityVector
struct Thing {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cThing, Thing);

int main() {
  const std::vector<Thing> things{{1}, {2}};
  (void)dreamor::index_set::relational::from<cThing>(things).build();
}
#endif  // DREAMOR_CASE_ref_column_from_a_vector

#ifdef DREAMOR_CASE_ref_column_from_pointers
// expect: A Ref column takes its entities from their EntityVector
struct Thing {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cThing, Thing);

int main() {
  static const std::vector<Thing> things{{1}, {2}};
  const auto things_of = [](cI) { return std::vector<const Thing*>{&things[0]}; };
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cThing>(things_of).build();
}
#endif  // DREAMOR_CASE_ref_column_from_pointers

#ifdef DREAMOR_CASE_text_column_from_a_returned_vector
// expect: A text column shows the text inside its source's std::strings
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);

int main() {
  const std::vector<std::vector<std::string>> grades_of{{"A", "B"}, {"C"}};
  const auto grades = [&grades_of](cI i) { return grades_of[i]; };  // a copy, gone after the call
  (void)dreamor::index_set::relational::from<cI>(I).lateral<cGrade>(grades).build();
}
#endif  // DREAMOR_CASE_text_column_from_a_returned_vector

#ifdef DREAMOR_CASE_text_column_from_a_temporary
// expect: A text column shows the text inside its source's std::strings
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);
std::vector<std::string> grades();

int main() {
  (void)dreamor::index_set::relational::from<cGrade>(grades()).build();
}
#endif  // DREAMOR_CASE_text_column_from_a_temporary

#ifdef DREAMOR_CASE_text_column_from_braced_strings
// expect: A text column shows the text inside its source's std::strings
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);
std::string grade();

int main() {
  (void)dreamor::index_set::relational::from<cGrade>({grade(), grade()}).build();
}
#endif  // DREAMOR_CASE_text_column_from_braced_strings

#ifdef DREAMOR_CASE_integer_column_from_doubles
// expect: A column takes values of its own type
int main() {
  const std::vector<double> raw{1.5, 2.0};
  return static_cast<int>(dreamor::index_set::relational::from<cI>(raw).build().size());
}
#endif  // DREAMOR_CASE_integer_column_from_doubles

#ifdef DREAMOR_CASE_ref_to_a_const_entity
// expect: Write Ref<Machine>, not Ref<const Machine>
struct Gear {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

int main() {
  return static_cast<int>(sizeof(dreamor::Ref<const Gear>));
}
#endif  // DREAMOR_CASE_ref_to_a_const_entity

#ifdef DREAMOR_CASE_ref_to_a_pointer
// expect: A Ref refers to an entity: Ref<Machine>, not Ref<Machine*>
struct Gear {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

int main() {
  return static_cast<int>(sizeof(dreamor::Ref<Gear*>));
}
#endif  // DREAMOR_CASE_ref_to_a_pointer

#ifdef DREAMOR_CASE_format_buffer_of_one
// expect: N is at least 2
int main() {
  return static_cast<int>(sizeof(dreamor::FormatBuffer<1>));
}
#endif  // DREAMOR_CASE_format_buffer_of_one

#ifdef DREAMOR_CASE_text_column_from_a_temporary_string
// expect: this std::string is a temporary, gone at the end of the statement
DREAMOR_VALUE_COLUMN(cLabel, std::string_view);

std::string trimmed(const std::string& field) {
  return field.substr(1);
}

int main() {
  const std::string field = " press-line-number-one";
  const cLabel label{trimmed(field)};
  return static_cast<int>(label.get().size());
}
#endif  // DREAMOR_CASE_text_column_from_a_temporary_string
