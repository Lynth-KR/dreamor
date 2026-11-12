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
#include <vector>

#ifdef DREAMOR_CASE_bound_is_not_generic
// expect: Cannot read this callable's parameter list
int main() {
  IloEnv env;
  dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, [](auto i) { return i.get(); }};
}
#endif  // DREAMOR_CASE_bound_is_not_generic

#ifdef DREAMOR_CASE_bound_takes_columns
// expect: Take the columns themselves
int main() {
  IloEnv env;
  dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, [](std::size_t i) { return i; }};
}
#endif  // DREAMOR_CASE_bound_takes_columns

#ifdef DREAMOR_CASE_vartable_bound_reads_missing_column
// expect: takes a column these rows do not have
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, [](cK k) { return k.get(); }};
}
#endif  // DREAMOR_CASE_vartable_bound_reads_missing_column

#ifdef DREAMOR_CASE_vartable_call_missing_key
// expect: Look a variable or value up by the table's key columns
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  (void)x(cI{1});
}
#endif  // DREAMOR_CASE_vartable_call_missing_key

#ifdef DREAMOR_CASE_vartable_call_raw_values
// expect: Look a variable or value up by the table's key columns
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  (void)x(2, 1);
}
#endif  // DREAMOR_CASE_vartable_call_raw_values

#ifdef DREAMOR_CASE_vartable_call_wrong_column
// expect: Look a variable or value up by the table's key columns
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  (void)x(cI{1}, cK{1});
}
#endif  // DREAMOR_CASE_vartable_call_wrong_column

#ifdef DREAMOR_CASE_vartable_call_wrong_order
// expect: Look a variable or value up by the table's key columns
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  (void)x(cJ{1}, cI{1});
}
#endif  // DREAMOR_CASE_vartable_call_wrong_order

#ifdef DREAMOR_CASE_vartable_call_value_of_another_type
// expect: Look a variable or value up by the table's key columns
DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);

int main() {
  IloEnv env;
  const std::vector<std::uint32_t> periods{1, 2};
  const dreamor::concert::VarTable<cI, cPeriod> x{
      env, dreamor::index_set::relational::from<cI>(I).cross<cPeriod>(periods).build()};
  (void)x(1, 2.0);
}
#endif  // DREAMOR_CASE_vartable_call_value_of_another_type

#ifdef DREAMOR_CASE_vartable_call_sibling_column_of_one_type
// expect: Look a variable or value up by the table's key columns
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  (void)x(cI{1}, cK{1});
}
#endif  // DREAMOR_CASE_vartable_call_sibling_column_of_one_type

#ifdef DREAMOR_CASE_vartable_has_no_add
int main() {
  IloEnv env;
  dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  x.add(cI{1}, cJ{1});
}
#endif  // DREAMOR_CASE_vartable_has_no_add

#ifdef DREAMOR_CASE_vartable_is_not_copyable
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ()};
  const auto y = x;
  (void)y;
}
#endif  // DREAMOR_CASE_vartable_is_not_copyable

#ifdef DREAMOR_CASE_vartable_key_order_differs_from_set
// expect: A VarTable is built on a set with exactly its key columns
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cJ, cI> x{env, IJ()};
}
#endif  // DREAMOR_CASE_vartable_key_order_differs_from_set

#ifdef DREAMOR_CASE_vartable_key_twice
// expect: names one key column twice
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cI> x{env, IJ(), 0, 1};
}
#endif  // DREAMOR_CASE_vartable_key_twice

#ifdef DREAMOR_CASE_vartable_needs_env_and_set
int main() {
  dreamor::concert::VarTable<cI, cJ> x;
  (void)x;
}
#endif  // DREAMOR_CASE_vartable_needs_env_and_set

#ifdef DREAMOR_CASE_vartable_scaled
// expect: Scale the sum, not the table
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, 1};
  (void)dreamor::concert::sumExpr(env, IJ(), 100 * x).build();
}
#endif  // DREAMOR_CASE_vartable_scaled

#ifdef DREAMOR_CASE_vartable_type_is_not_bool
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, 1, true, "x"};
}
#endif  // DREAMOR_CASE_vartable_type_is_not_bool

#ifdef DREAMOR_CASE_vartable_bound_returns_a_ref
// expect: A VarTable's bound is a number, and this callable returns something else
struct Gear {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cGear, Gear);

int main() {
  IloEnv env;
  dreamor::EntityVector<Gear> gears;
  static_cast<void>(gears.emplace(Gear{1}));
  gears.freeze();
  const auto sGEARS = dreamor::index_set::relational::from<cGear>(gears).build();
  const dreamor::concert::VarTable<cGear> x{env, sGEARS, 0, [](cGear g) { return g.get(); }};
}
#endif  // DREAMOR_CASE_vartable_bound_returns_a_ref

#ifdef DREAMOR_CASE_vartable_bound_from_a_vector
// expect: A VarTable's bound is a number or a callable over the row's columns
int main() {
  IloEnv env;
  const std::vector<double> upper{1, 2, 3, 4};
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, upper};
}
#endif  // DREAMOR_CASE_vartable_bound_from_a_vector

#ifdef DREAMOR_CASE_vartable_name_in_the_type_place
// expect: A VarTable takes the variables' type before their name
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, 1, "x"};
  return static_cast<int>(x.size());
}
#endif  // DREAMOR_CASE_vartable_name_in_the_type_place

#ifdef DREAMOR_CASE_vartable_null_name
// expect: A name is text, and nullptr is not one
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, 1, ILOFLOAT, nullptr};
  return static_cast<int>(x.size());
}
#endif  // DREAMOR_CASE_vartable_null_name
