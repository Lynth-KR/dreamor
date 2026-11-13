// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"

#ifdef DREAMOR_CASE_where_is_not_generic
// expect: Cannot read this callable's parameter list
int main() {
  (void)IJ().where([](auto) { return true; });
}
#endif  // DREAMOR_CASE_where_is_not_generic

#ifdef DREAMOR_CASE_where_reads_a_later_column
// expect: lateral and where read only the columns before them in the chain
int main() {
  const auto small = [](cJ j) { return j.get() < 2; };
  (void)dreamor::index_set::relational::from<cI>(I).where(small).cross<cJ>(I).build();
}
#endif  // DREAMOR_CASE_where_reads_a_later_column

#ifdef DREAMOR_CASE_where_reads_missing_column
// expect: takes a column this set does not have
// when: A `where` predicate reads a column absent from the set.
//
// fix: Same.
int main() {
  (void)IJ().where([](cK k) { return k.get() > 1; });
}
#endif  // DREAMOR_CASE_where_reads_missing_column

#ifdef DREAMOR_CASE_where_returns_nothing
// when: A condition that returns nothing (`void`).
//
// fix: Return whether the row is kept: `[](cI i) { return i.get() > 1; }`.
int main() {
  (void)IJ().where([](cI i) { (void)i; });
}
#endif  // DREAMOR_CASE_where_returns_nothing

#ifdef DREAMOR_CASE_where_takes_column_twice
// expect: This callable takes the same column twice
int main() {
  (void)IJ().where([](cI a, cI b) { return a.get() < b.get(); });
}
#endif  // DREAMOR_CASE_where_takes_column_twice

#ifdef DREAMOR_CASE_where_takes_columns
// expect: Take the columns themselves
int main() {
  (void)IJ().where([](std::size_t i) { return i > 1; });
}
#endif  // DREAMOR_CASE_where_takes_columns

#ifdef DREAMOR_CASE_where_takes_a_column_by_reference
// expect: Take each column by value
int main() {
  const auto sIJ = IJ();
  return static_cast<int>(sIJ.where([](cI& /*i*/) { return true; }).size());
}
#endif  // DREAMOR_CASE_where_takes_a_column_by_reference

#ifdef DREAMOR_CASE_chain_where_takes_a_column_by_reference
// expect: Take each column by value
int main() {
  return static_cast<int>(
      dreamor::index_set::relational::from<cI>(I).where([](cI& /*i*/) { return true; }).build().size());
}
#endif  // DREAMOR_CASE_chain_where_takes_a_column_by_reference

#ifdef DREAMOR_CASE_where_without_a_predicate
// expect: where() takes the predicates it applies: S.where(is_open)
// when: `S.where()` or a chain's `.where()` with no predicate.
//
// fix: `S.where(is_open)`, or several at once.
int main() {
  return static_cast<int>(IJ().where().size());
}
#endif  // DREAMOR_CASE_where_without_a_predicate

#ifdef DREAMOR_CASE_chain_where_without_a_predicate
// expect: where() takes the predicates it applies: .where(is_open)
// when: A chain's `where()` with no condition.
//
// fix: Give it the conditions to apply, `.where(is_open)`, or drop the step.
int main() {
  return static_cast<int>(dreamor::index_set::relational::from<cI>(I).where().build().size());
}
#endif  // DREAMOR_CASE_chain_where_without_a_predicate

#ifdef DREAMOR_CASE_where_reads_no_column
// expect: The predicate reads no columns, so it would be a constant
// when: `S.where([] { return true; })`.
//
// fix: Take the columns it tests.
int main() {
  return static_cast<int>(IJ().where([] { return true; }).size());
}
#endif  // DREAMOR_CASE_where_reads_no_column

#ifdef DREAMOR_CASE_where_is_a_function_name
// expect: is an indirect call per candidate row
// when: A function name or a `std::function` as a `where` predicate.
//
// fix: A lambda or a function object.
bool keep(cI /*i*/) {
  return true;
}

int main() {
  return static_cast<int>(IJ().where(keep).size());
}
#endif  // DREAMOR_CASE_where_is_a_function_name

#ifdef DREAMOR_CASE_reads_cannot_call
// expect: dreamor::reads<Columns...>(f) calls f with those columns, in that order, and f cannot be called so
// when: `reads<cI, cJ>(f)` where `f` takes another number or kind of columns.
//
// fix: List the columns that `f` takes, in the order of its parameters.
int main() {
  const auto one_column = [](const auto& i) { return i.get() > 1; };
  const auto sIJ        = dreamor::index_set::relational::from<cI>(I).cross<cJ>(I).build();
  return static_cast<int>(sIJ.where(dreamor::reads<cI, cJ>(one_column)).size());  // two columns for one parameter
}
#endif  // DREAMOR_CASE_reads_cannot_call

#ifdef DREAMOR_CASE_reads_names_a_column_twice
// expect: reads<Columns...>(f) names each column once
// when: A column named twice in `reads<...>`.
//
// fix: Name each column once.
int main() {
  const auto both = [](const auto& a, const auto& b) { return a.get() < b.get(); };
  const auto sI   = dreamor::index_set::relational::from<cI>(I).build();
  return static_cast<int>(sI.where(dreamor::reads<cI, cI>(both)).size());
}
#endif  // DREAMOR_CASE_reads_names_a_column_twice

#ifdef DREAMOR_CASE_reads_takes_columns
// expect: reads<Columns...>(f) takes columns declared with DREAMOR_ENTITY_COLUMN or DREAMOR_VALUE_COLUMN
// when: A type that is not a column in `reads<...>`.
//
// fix: Name the columns: `reads<cMachine, cPeriod>(f)`.
int main() {
  const auto positive = [](const auto& v) { return v > 0; };
  const auto sI       = dreamor::index_set::relational::from<cI>(I).build();
  return static_cast<int>(sI.where(dreamor::reads<std::size_t>(positive)).size());
}
#endif  // DREAMOR_CASE_reads_takes_columns

#ifdef DREAMOR_CASE_reads_names_no_column
// expect: reads<Columns...>(f) names the columns that f reads
// when: `reads<>(f)` or `reads(f)`, with no column.
//
// fix: Name the columns that `f` takes: `reads<cMachine>(f)`.
int main() {
  const auto any = [](const auto& /*row*/) { return true; };
  return static_cast<int>(IJ().where(dreamor::reads<>(any)).size());
}
#endif  // DREAMOR_CASE_reads_names_no_column
