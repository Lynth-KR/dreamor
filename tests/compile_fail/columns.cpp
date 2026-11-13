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
// when: `DREAMOR_VALUE_COLUMN(cMachine, const Machine*)`.
//
// fix: `DREAMOR_ENTITY_COLUMN(cMachine, Machine)`, filled from the machines' `EntityVector`.
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
// when: A column of a type with no `==`, such as a struct of the user's.
//
// fix: A `Ref` to an entity, an integer, an enum, a `char`, a text (`std::string_view`, `std::string`), or give the
// type `==`.
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
// when: A column of a type of the user's with `==` and no `std::hash`.
//
// fix: Specialize `std::hash` for the type before the column is declared.
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
// when: `DREAMOR_VALUE_COLUMN(cWeight, double)`.
//
// fix: An integer, an enum, a `char` or a text; a quantity such as a weight is data on a set (`ParamTable`) or an
// entity's attribute, not a column; a measured value that is itself a set's member, such as a breakpoint, goes in a
// type of yours with `==` and `std::hash`.
DREAMOR_VALUE_COLUMN(cWeight, double);

int main() {
  const cWeight weight{1.5};
  return weight > 1.0 ? 0 : 1;
}
#endif  // DREAMOR_CASE_column_holds_a_double

#ifdef DREAMOR_CASE_column_holds_a_wchar_t
// expect: A column holds a char, not a wchar_t
// when: A column of `wchar_t`, `char8_t`, `char16_t` or `char32_t`.
//
// fix: A `std::string_view` of the text, or its code as an integer.
DREAMOR_VALUE_COLUMN(cWide, wchar_t);

int main() {
  const cWide wide{L'a'};
  return static_cast<int>(wide.get());
}
#endif  // DREAMOR_CASE_column_holds_a_wchar_t

#ifdef DREAMOR_CASE_ref_column_from_a_returned_vector
// expect: A Ref column takes its entities from their EntityVector
// when: A `Ref` column fed from a `std::vector` of entities or of pointers - a container source, a lateral column's
// range, or `sumExpr<cX>` or an aggregate such as `countIf<cX>` over a container - or from an `EntityVector` a lateral
// column returns by value.
//
// fix: Give the `EntityVector` itself, or the `Ref`s it gave (`at`, `find`).
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
// when: A column of `std::string_view` fed from a temporary container of `std::string`s (braced ones too), from a range
// that makes its strings, or from a lateral source that returns its strings by value.
//
// fix: A named container or a view of one (`std::span`); a lateral source that returns a reference (`-> const
// std::vector<std::string>&`) or a `std::span`.
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
// when: A column of integers filled from a `std::vector<double>`, or any column from values of a type that is not its
// own.
//
// fix: Values of the column's type, any integer for a column of integers, text for a column of `std::string_view`; or
// convert each value on purpose.
int main() {
  const std::vector<double> raw{1.5, 2.0};
  return static_cast<int>(dreamor::index_set::relational::from<cI>(raw).build().size());
}
#endif  // DREAMOR_CASE_integer_column_from_doubles

#ifdef DREAMOR_CASE_ref_to_a_const_entity
// expect: Write Ref<Machine>, not Ref<const Machine>
// when: `Ref<const Machine>`.
//
// fix: `Ref<Machine>`: a `Ref` only reads its entity.
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
// when: `Ref<Machine*>`, `Ref<Machine&>`.
//
// fix: `Ref<Machine>`.
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
// when: `FormatBuffer<1>`, `NameBuilder<1>`.
//
// fix: `N` of 2 or more: room for one character and the null terminator.
int main() {
  return static_cast<int>(sizeof(dreamor::FormatBuffer<1>));
}
#endif  // DREAMOR_CASE_format_buffer_of_one

#ifdef DREAMOR_CASE_text_column_from_a_temporary_string
// expect: this std::string is a temporary, gone at the end of the statement
// when: A column of `std::string_view` made from a `std::string` a call returns, `cLabel{trim(field)}`.
//
// fix: Declare the column over `std::string`, which holds its text, or show the record's own field.
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
