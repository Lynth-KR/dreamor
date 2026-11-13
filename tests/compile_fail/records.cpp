// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <cstdint>
#include <ranges>
#include <tuple>
#include <vector>

#ifdef DREAMOR_CASE_indexset_records_columns_not_deduced
// expect: IndexSet{records, row_of} does not deduce its columns
// when: `dreamor::IndexSet sROUTES{routes, route_of}`: C++20 cannot read the columns off what `row_of` returns.
//
// fix: Name the set's type: `IndexSet<cMachine, cItem> sROUTES{routes, route_of}`.
int main() {
  const auto row_of = [](std::size_t i) { return cI{i}; };
  const dreamor::IndexSet sI{I, row_of};
  return static_cast<int>(sI.size());
}
#endif  // DREAMOR_CASE_indexset_records_columns_not_deduced

#ifdef DREAMOR_CASE_paramtable_records_type_not_deduced
// expect: ParamTable{records, row_of, value_of} does not deduce its type
// when: `dreamor::ParamTable capacity{machines, machine_of, capacity_of}`: the key columns cannot be read off what
// `row_of` returns.
//
// fix: Name the table's type: `ParamTable<double, cMachine> capacity{machines, machine_of, capacity_of}`.
int main() {
  const auto row_of   = [](std::size_t i) { return cI{i}; };
  const auto value_of = [](std::size_t i) { return 2.0 * static_cast<double>(i); };
  const dreamor::ParamTable hours{I, row_of, value_of};
  return static_cast<int>(hours.size());
}
#endif  // DREAMOR_CASE_paramtable_records_type_not_deduced

#ifdef DREAMOR_CASE_indexset_records_not_a_range
// expect: makes a row of each record: give a range of records
// when: `IndexSet<...>{x, row_of}` with `x` not a range.
//
// fix: Pass a container or a view of the records.
int main() {
  const auto row_of     = [](std::size_t i) { return cI{i}; };
  const std::size_t one = 1;
  const dreamor::IndexSet<cI> sI{one, row_of};
  return static_cast<int>(sI.size());
}
#endif  // DREAMOR_CASE_indexset_records_not_a_range

#ifdef DREAMOR_CASE_indexset_records_other_columns
// expect: row_of returns other columns than the set's
// when: `row_of` returns columns the set does not have, or misses one.
//
// fix: Return the set's columns, in any order: `std::tuple<cMachine, cItem>{m, i}`.
int main() {
  const auto row_of = [](std::size_t i) { return std::tuple<cI, cK>{i, i}; };
  const dreamor::IndexSet<cI, cJ> sIJ{I, row_of};
  return static_cast<int>(sIJ.size());
}
#endif  // DREAMOR_CASE_indexset_records_other_columns

#ifdef DREAMOR_CASE_indexset_records_row_of_takes_another_type
// expect: row_of takes one record and returns its row
// when: `row_of` does not take the records' element type.
//
// fix: Take the record's type, or `const auto&`.
int main() {
  const auto row_of = [](const std::string& name) { return cI{name.size()}; };
  const dreamor::IndexSet<cI> sI{I, row_of};
  return static_cast<int>(sI.size());
}
#endif  // DREAMOR_CASE_indexset_records_row_of_takes_another_type

#ifdef DREAMOR_CASE_indexset_records_values_share_a_type
// expect: row_of returns plain values, and a value fills the column of its type
// when: A plain value two columns could take - two columns of its type, or an integer of another type where the set has
// two columns of integers - or one no column takes.
//
// fix: Return the columns by name: `std::tuple<cFrom, cTo>{a, b}`.
int main() {
  const auto row_of = [](std::size_t i) { return std::tuple{i, i + 1}; };
  const dreamor::IndexSet<cI, cJ> sIJ{I, row_of};
  return static_cast<int>(sIJ.size());
}
#endif  // DREAMOR_CASE_indexset_records_values_share_a_type

#ifdef DREAMOR_CASE_indexset_records_integer_with_two_integer_columns
// expect: row_of returns plain values, and a value fills the column of its type
DREAMOR_VALUE_COLUMN(cShift, int);

int main() {
  const auto row_of = [](std::size_t i) { return std::tuple{i, static_cast<std::int64_t>(i)}; };
  const dreamor::IndexSet<cI, cShift> sIS{I, row_of};
  return static_cast<int>(sIS.size());
}
#endif  // DREAMOR_CASE_indexset_records_integer_with_two_integer_columns

#ifdef DREAMOR_CASE_indexset_records_text_from_a_temporary
// expect: so the records have to outlive the set
// when: `IndexSet<cName>{loadRecords(), name_of}`: a set with a text column built from a temporary range of records, a
// copy, or a range that makes its records.
//
// fix: Name the records (`const auto records = loadRecords();`), or hand a view of a named container.
DREAMOR_VALUE_COLUMN(cLabel, std::string_view);

struct Named {
  std::string name;
};

int main() {
  const auto name_of = [](const Named& n) { return std::string_view{n.name}; };
  const dreamor::IndexSet<cLabel> sNAMES{std::vector<Named>{{"press-line-number-one"}}, name_of};
  return static_cast<int>(sNAMES.size());
}
#endif  // DREAMOR_CASE_indexset_records_text_from_a_temporary

#ifdef DREAMOR_CASE_indexset_records_text_from_a_copied_record
// expect: row_of takes each record by value
// when: `IndexSet<cLabel>{records, [](Record r) { return Label{r.name}; }}` - a text column from a copy of each record.
//
// fix: Take the record by reference: `[](const Record& r)`.
DREAMOR_VALUE_COLUMN(cLabel, std::string_view);

struct Named {
  std::string name;
};

int main() {
  const std::vector<Named> named{{"press-line-number-one"}};
  const auto name_of = [](Named n) { return std::string_view{n.name}; };
  const dreamor::IndexSet<cLabel> sNAMES{named, name_of};
  return static_cast<int>(sNAMES.size());
}
#endif  // DREAMOR_CASE_indexset_records_text_from_a_copied_record

#ifdef DREAMOR_CASE_indexset_of_a_non_column
// expect: An IndexSet's type lists its columns
// when: `IndexSet<int>{records, row_of}`, `IndexSet<Machine, Item>`.
//
// fix: `IndexSet<cMachine, cItem>`.
struct Record {
  std::size_t a;
};

int main() {
  const std::vector<Record> records{{1}};
  const auto row_of = [](const Record& r) { return std::tuple<std::size_t>{r.a}; };
  const dreamor::IndexSet<std::size_t> s{records, row_of};
  return static_cast<int>(s.size());
}
#endif  // DREAMOR_CASE_indexset_of_a_non_column

#ifdef DREAMOR_CASE_indexset_records_text_from_a_returned_string
// expect: row_of returns a std::string for a column of std::string_view
// when: `row_of` returns text it made (`trim(field)`, `substr`) for a column of `std::string_view`, which would show it
// after the call.
//
// fix: Declare the column over `std::string`, or return the record's own text.
DREAMOR_VALUE_COLUMN(cLabel, std::string_view);

struct Named {
  std::string name;
};

int main() {
  const std::vector<Named> named{{" press-line-number-one"}};
  const auto name_of = [](const Named& n) { return std::tuple{n.name.substr(1)}; };
  const dreamor::IndexSet<cLabel> sNAMES{named, name_of};
  return static_cast<int>(sNAMES.size());
}
#endif  // DREAMOR_CASE_indexset_records_text_from_a_returned_string
