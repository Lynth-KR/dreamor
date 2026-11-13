// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include <ranges>
#include <sstream>
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#ifdef DREAMOR_CASE_paramtable_call_raw_values
// expect: Look a variable or value up by the table's key columns
// when: `VarTable`/`SolutionTable`/`ParamTable` lookup with a bare value or a column of another name where two key
// columns share its type (`x(2, 1)` over `cI, cJ` both `std::size_t`), a value of no key column's type, a wrong column,
// wrong order, or a missing key column.
//
// fix: Pass the key's columns in declaration order, or one row; a value goes where its type is one key column's alone -
// where two share it, name each value's column (`cFrom{a}, cTo{b}`).
int main() {
  const dreamor::ParamTable<double, cI, cJ> hours{IJ(), 8.0};
  return static_cast<int>(hours(2, 1));
}
#endif  // DREAMOR_CASE_paramtable_call_raw_values

#ifdef DREAMOR_CASE_paramtable_deduced_from_a_generic_callable
// expect: Cannot read this callable's parameter list
int main() {
  const auto hours_of = [](auto i, auto j) { return static_cast<double>(i.get() + j.get()); };
  const dreamor::ParamTable hours{IJ(), hours_of};  // the type deduced: the callable is still read for its columns
  return static_cast<int>(hours.size());
}
#endif  // DREAMOR_CASE_paramtable_deduced_from_a_generic_callable

#ifdef DREAMOR_CASE_paramtable_deduced_from_a_vector
// expect: not from a container, whose order no row knows
int main() {
  const std::vector<double> hours{1, 2, 3, 4};
  const dreamor::ParamTable table{IJ(), hours};  // the type deduced: the vector is reported, not held in each row
  return static_cast<int>(table.size());
}
#endif  // DREAMOR_CASE_paramtable_deduced_from_a_vector

#ifdef DREAMOR_CASE_paramtable_deduced_from_a_generic_callable_of_some_columns
// expect: Cannot read this callable's parameter list
int main() {
  const auto hours_of = [](auto i) { return static_cast<double>(i.get()); };
  const dreamor::ParamTable hours{IJ(), hours_of};  // one auto parameter over two columns: not a value to hold
  return static_cast<int>(hours.size());
}
#endif  // DREAMOR_CASE_paramtable_deduced_from_a_generic_callable_of_some_columns

#ifdef DREAMOR_CASE_paramtable_deduced_from_a_callable_of_another_column
// expect: takes a column these rows do not have
int main() {
  const auto hours_of = [](cK k) { return static_cast<double>(k.get()); };
  const dreamor::ParamTable hours{IJ(), hours_of};
  return static_cast<int>(hours.size());
}
#endif  // DREAMOR_CASE_paramtable_deduced_from_a_callable_of_another_column

#ifdef DREAMOR_CASE_paramtable_holds_a_pointer
// expect: A ParamTable refers to an entity by a Ref, not a pointer
// when: `ParamTable<const Machine*, ...>`.
//
// fix: `ParamTable<dreamor::Ref<Machine>, ...>`.
struct Machine {
  std::size_t id;
};

int main() {
  static const Machine m1{1};
  const auto machine_of = [](cI) { return &m1; };
  const dreamor::ParamTable<const Machine*, cI, cJ> assigned{IJ(), machine_of};
  return static_cast<int>(assigned.size());
}
#endif  // DREAMOR_CASE_paramtable_holds_a_pointer

#ifdef DREAMOR_CASE_paramtable_holds_a_reference
// expect: A ParamTable holds a value a row, copied in
// when: `ParamTable<const double&, ...>`: a reference, an array or a `const` type.
//
// fix: The value type itself: a number, a `bool`, an enum, a text, a `Ref`, or a struct of the row's values
// (`ParamTable<Lane, cFrom, cTo>`).
int main() {
  const auto hours_of = [](cI, cJ) -> const double& {
    static const double hours = 8.0;
    return hours;
  };
  const dreamor::ParamTable<const double&, cI, cJ> runs{IJ(), hours_of};
  return static_cast<int>(runs.size());
}
#endif  // DREAMOR_CASE_paramtable_holds_a_reference

#ifdef DREAMOR_CASE_paramtable_key_order_differs_from_set
// expect: A ParamTable is built on a set with exactly its key columns
// when: A ParamTable whose keys differ from the set's columns or their order.
//
// fix: Match the table to the set, or `select<...>()` to reorder.
int main() {
  const dreamor::ParamTable<double, cJ, cI> x{IJ(), 1.0};
}
#endif  // DREAMOR_CASE_paramtable_key_order_differs_from_set

#ifdef DREAMOR_CASE_paramtable_value_returns_another_type
// expect: or a callable over the row's columns that returns a T
// when: A ParamTable's callable returns something not convertible to its `T`.
//
// fix: Return a `T`.
struct Window {
  std::size_t first, last;
};

int main() {
  const auto window_of = [](cI i) { return Window{i.get(), i.get() + 1}; };
  const dreamor::ParamTable<double, cI, cJ> hours{IJ(), window_of};
  return static_cast<int>(hours.size());
}
#endif  // DREAMOR_CASE_paramtable_value_returns_another_type

#ifdef DREAMOR_CASE_solutiontable_call_raw_values
// expect: Look a variable or value up by the table's key columns
struct Algo {
  IloEnv env;

  IloEnv getEnv() const { return env; }

  void getValues(IloNumArray, const IloNumVarArray) const {}
};

int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  const auto plan = x.solution(Algo{env});
  (void)plan(2, 1);
}
#endif  // DREAMOR_CASE_solutiontable_call_raw_values

#ifdef DREAMOR_CASE_paramtable_text_from_a_returned_string
// expect: returns a std::string by value, gone once the call ends
// when: `ParamTable<std::string_view, cItem>{sITEMS, [](cItem i) { return i->name; }}` - a callable that makes the text
// it returns.
//
// fix: Return a reference to a string that outlives the table: `-> const std::string&`, or a `std::string_view` of it.
int main() {
  const auto label_of = [](cI i) { return std::to_string(i.get()); };
  const dreamor::ParamTable<std::string_view, cI> labels{IJ().select<cI>(), label_of};
  return static_cast<int>(labels.size());
}
#endif  // DREAMOR_CASE_paramtable_text_from_a_returned_string

#ifdef DREAMOR_CASE_paramtable_value_from_a_vector
// expect: not from a container, whose order no row knows
// when: `ParamTable<double, cMachine>{sMACHINES, hours}` with `hours` a container.
//
// fix: A callable over the row's columns that reads it: `[&hours](cMachine m) { return hours.at(m->id); }`.
int main() {
  const std::vector<double> hours{1, 2, 3, 4};
  const dreamor::ParamTable<double, cI, cJ> table{IJ(), hours};
  return static_cast<int>(table.size());
}
#endif  // DREAMOR_CASE_paramtable_value_from_a_vector

#ifdef DREAMOR_CASE_paramtable_value_of_another_type
// expect: This value is not a T, nor a callable
// when: `ParamTable<std::string_view, cMachine>{sMACHINES, 3}`: one value of another type.
//
// fix: One value of the table's `T`, or a callable over the row's columns.
int main() {
  const dreamor::ParamTable<std::string_view, cI, cJ> table{IJ(), 3};
  return static_cast<int>(table.size());
}
#endif  // DREAMOR_CASE_paramtable_value_of_another_type

#ifdef DREAMOR_CASE_paramtable_value_converts_to_the_type
// expect: One value for every row is a T itself, not a value that converts to one
// when: `ParamTable<std::string_view, cMachine>{sMACHINES, label}` with `label` a `std::string`.
//
// fix: One value of the table's `T`: `std::string_view{label}`, with `label` outliving the table.
int main() {
  const std::string label = "x";
  const dreamor::ParamTable<std::string_view, cI, cJ> table{IJ(), label};
  return static_cast<int>(table.size());
}
#endif  // DREAMOR_CASE_paramtable_value_converts_to_the_type

#ifdef DREAMOR_CASE_paramtable_records_value_of_takes_another_type
// expect: value_of takes one record and returns its value
// when: `ParamTable{records, row_of, value_of}` whose `value_of` does not take the records' element type.
//
// fix: Take the record's type, or `const auto&`.
struct Arc {
  std::size_t from, to;
  double cost;
};

int main() {
  const std::vector<Arc> arcs{{1, 2, 3.0}};
  const auto arc_of  = [](const Arc& a) { return std::tuple<cI, cJ>{a.from, a.to}; };
  const auto cost_of = [](const std::string& code) { return static_cast<double>(code.size()); };
  const dreamor::ParamTable<double, cI, cJ> cost{arcs, arc_of, cost_of};
  return static_cast<int>(cost.size());
}
#endif  // DREAMOR_CASE_paramtable_records_value_of_takes_another_type

#ifdef DREAMOR_CASE_paramtable_records_value_of_returns_another_type
// expect: value_of returns something else than the table's T
// when: `ParamTable{records, row_of, value_of}` whose `value_of` returns what does not convert to `T`.
//
// fix: Return a `T`, or a table of the type it returns.
struct Arc {
  std::size_t from, to;
};

struct Window {
  std::size_t first, last;
};

int main() {
  const std::vector<Arc> arcs{{1, 2}};
  const auto arc_of    = [](const Arc& a) { return std::tuple<cI, cJ>{a.from, a.to}; };
  const auto window_of = [](const Arc& a) { return Window{a.from, a.to}; };
  const dreamor::ParamTable<double, cI, cJ> cost{arcs, arc_of, window_of};
  return static_cast<int>(cost.size());
}
#endif  // DREAMOR_CASE_paramtable_records_value_of_returns_another_type

#ifdef DREAMOR_CASE_paramtable_records_read_once
// expect: reads the records twice, for the rows and for the values
// when: `ParamTable{records, row_of, value_of}` over a range read once, such as `std::views::istream`.
//
// fix: A container of the records, or a view of one.
int main() {
  std::istringstream in{"1 2 3"};
  auto records         = std::views::istream<std::size_t>(in);
  const auto row_of    = [](std::size_t r) { return cI{r}; };
  const auto weight_of = [](std::size_t r) { return static_cast<double>(r); };
  const dreamor::ParamTable<double, cI> weight{records, row_of, weight_of};
  return static_cast<int>(weight.size());
}
#endif  // DREAMOR_CASE_paramtable_records_read_once

#ifdef DREAMOR_CASE_paramtable_records_text_from_temporary_records
// expect: of texts shows each text where value_of found it
// when: A `std::string_view` table from temporary records, from a `value_of` that takes the record by value, or returns
// a `std::string`.
//
// fix: Named records taken by reference and the record's own text, or a table over `std::string`.
struct Arc {
  std::size_t from, to;
  std::string label;
};

int main() {
  const auto arc_of   = [](const Arc& a) { return std::tuple<cI, cJ>{a.from, a.to}; };
  const auto label_of = [](const Arc& a) -> const std::string& { return a.label; };
  const dreamor::ParamTable<std::string_view, cI, cJ> label{
      std::vector<Arc>{{1, 2, "the-first-arc-label"}}, arc_of, label_of};
  return static_cast<int>(label.size());
}
#endif  // DREAMOR_CASE_paramtable_records_text_from_temporary_records

#ifdef DREAMOR_CASE_table_key_is_an_entity_primary_key
// expect: A column of entities takes a Ref to the entity, not its primary key
// when: A text in an entity column's key place, `hours("M1")`.
//
// fix: Look the entity up: `hours(machines.at("M1"))`.
struct Machine {
  std::string name;

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

DREAMOR_ENTITY_COLUMN(cMachine, Machine);

int main() {
  dreamor::EntityVector<Machine> machines;
  machines.insert(Machine{"M1"});
  machines.freeze();
  const auto sMACHINES = dreamor::index_set::relational::from<cMachine>(machines).build();
  const dreamor::ParamTable<double, cMachine> hours{sMACHINES, 8.0};
  return static_cast<int>(hours("M1"));
}
#endif  // DREAMOR_CASE_table_key_is_an_entity_primary_key

#ifdef DREAMOR_CASE_table_key_is_an_entity_object
// expect: A column of entities takes a Ref to the entity, not the entity object
// when: An `EntityVector`'s element in an entity column's key place, `for (const Machine& m : machines) hours(m)`.
//
// fix: Take its `Ref` from the vector, `machines.ref(m)`, or walk `machines.refs()` or a set of the column.
struct Machine {
  std::string name;

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

DREAMOR_ENTITY_COLUMN(cMachine, Machine);

int main() {
  dreamor::EntityVector<Machine> machines;
  machines.insert(Machine{"M1"});
  machines.freeze();
  const auto sMACHINES = dreamor::index_set::relational::from<cMachine>(machines).build();
  const dreamor::ParamTable<double, cMachine> hours{sMACHINES, 8.0};
  double total = 0;
  for (const Machine& machine : machines) {
    total += hours(machine);
  }
  return static_cast<int>(total);
}
#endif  // DREAMOR_CASE_table_key_is_an_entity_object
