// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that must not compile. Each case sits between `#ifdef DREAMOR_CASE_<name>` and its `#endif` and is
// compiled alone, with -DDREAMOR_CASE_<name> (tests/cases.py); the `// expect:` line right after its
// #ifdef, where a case has one, is what the compiler's first error and its notes must say.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <ilcplex/ilocplex.h>
#include <tuple>
#include <vector>

#ifdef DREAMOR_CASE_sumexpr_container_is_temporary
// expect: sumExpr keeps its rows by reference, so they cannot be a temporary container
int main() {
  IloEnv env;
  (void)dreamor::concert::sumExpr<cI>(env, std::vector<std::size_t>{1, 2}, [&env](cI i) {
    return IloExpr(env, i.get());
  }).build();
}
#endif  // DREAMOR_CASE_sumexpr_container_is_temporary

#ifdef DREAMOR_CASE_sumexpr_container_needs_column
// expect: sumExpr takes an index set
int main() {
  IloEnv env;
  (void)dreamor::concert::sumExpr(env, I, [&env](cI i) { return IloExpr(env, i.get()); }).build();
}
#endif  // DREAMOR_CASE_sumexpr_container_needs_column

#ifdef DREAMOR_CASE_sumexpr_ref_column_from_a_vector
// expect: A Ref column takes its entities from their EntityVector
struct Thing {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cThing, Thing);

int main() {
  IloEnv env;
  const std::vector<Thing> things{{1}, {2}};
  const auto one = [](cThing) { return 1.0; };
  (void)dreamor::concert::sumExpr<cThing>(env, things, one).build();
}
#endif  // DREAMOR_CASE_sumexpr_ref_column_from_a_vector

#ifdef DREAMOR_CASE_sumexpr_term_is_generic
// expect: Cannot read this callable's parameter list
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumExpr(env, sIJ, [&x](auto i, auto j) { return x(i, j); }).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_is_generic

#ifdef DREAMOR_CASE_sumexpr_term_reads_missing_column
// expect: takes a column these rows do not have
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumExpr(env, sIJ, [&x](cI i, cK k) { return x(i, k.get()); }).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_reads_missing_column

#ifdef DREAMOR_CASE_sumexpr_term_takes_column_twice
// expect: This callable takes the same column twice
int main() {
  IloEnv env;
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ(), 0, 1};
  (void)dreamor::concert::sumExpr(env, IJ(), [&x](cI a, cI b) { return x(a, cJ{b.get()}); }).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_takes_column_twice

#ifdef DREAMOR_CASE_sumexpr_term_takes_values
// expect: Take the columns themselves
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumExpr(env, sIJ, [&x](std::size_t i, std::size_t j) { return x(i, j); }).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_takes_values

#ifdef DREAMOR_CASE_sumexpr_where_each_twice
// expect: whereEach() once
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumExpr(env, sIJ, [&x](cI i, cJ j) { return x(i, j); })
      .whereEach([](cI) { return true; })
      .whereEach([](cJ) { return true; })
      .build();
}
#endif  // DREAMOR_CASE_sumexpr_where_each_twice

#ifdef DREAMOR_CASE_sumexpr_without_build
// expect: is not an expression yet
int main() {
  IloEnv env;
  IloModel model(env);
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  model.add(dreamor::concert::sumExpr(env, sIJ, [&x](cI i, cJ j) { return x(i, j); }) <= 1);
}
#endif  // DREAMOR_CASE_sumexpr_without_build

#ifdef DREAMOR_CASE_sumexpr_without_build_assigned
// expect: UnbuiltSum
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  IloExpr total = dreamor::concert::sumExpr(env, sIJ, x);
  (void)total;
}
#endif  // DREAMOR_CASE_sumexpr_without_build_assigned

#ifdef DREAMOR_CASE_sumexpr_without_build_objective
// expect: UnbuiltSum
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  IloModel model(env);
  model.add(IloMinimize(env, dreamor::concert::sumExpr(env, sIJ, x)));
}
#endif  // DREAMOR_CASE_sumexpr_without_build_objective

#ifdef DREAMOR_CASE_sumexpr_without_build_plus_assign
// expect: is not an expression yet
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  IloExpr total(env);
  total += dreamor::concert::sumExpr(env, sIJ, x);
}
#endif  // DREAMOR_CASE_sumexpr_without_build_plus_assign

#ifdef DREAMOR_CASE_sumexpr_without_build_reversed
// expect: is not an expression yet
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  const IloExpr base(env);
  (void)(base - dreamor::concert::sumExpr(env, sIJ, x));
}
#endif  // DREAMOR_CASE_sumexpr_without_build_reversed

#ifdef DREAMOR_CASE_sumexpr_without_env
// expect: takes the IloEnv first
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const dreamor::concert::VarTable<cI, cJ> x{env, sIJ};
  (void)dreamor::concert::sumExpr(sIJ, [&x](cI i, cJ j) { return x(i, j); }).build();
}
#endif  // DREAMOR_CASE_sumexpr_without_env

#ifdef DREAMOR_CASE_sumexpr_over_an_ordered
// expect: sumExpr sums over a range of rows
int main() {
  IloEnv env;
  const auto sIJ     = IJ();
  const auto ordered = sIJ.ordered();
  (void)dreamor::concert::sumExpr(env, ordered, [](cJ) { return 1.0; });
}
#endif  // DREAMOR_CASE_sumexpr_over_an_ordered

#ifdef DREAMOR_CASE_sumexpr_over_one_row
// expect: sumExpr sums over a range of rows
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  (void)dreamor::concert::sumExpr(env, *sIJ.begin(), [](cJ) { return 1.0; });
}
#endif  // DREAMOR_CASE_sumexpr_over_one_row

#ifdef DREAMOR_CASE_sumexpr_over_a_grouped
// expect: A Grouped is a range of groups, not of rows
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  const auto byI = sIJ.groupBy<cI>();
  (void)dreamor::concert::sumExpr(env, byI, [](cJ) { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_over_a_grouped

#ifdef DREAMOR_CASE_sumexpr_container_two_columns
// expect: One column type at most
int main() {
  IloEnv env;
  (void)dreamor::concert::sumExpr<cI, cJ>(env, I, [](cI) { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_container_two_columns

#ifdef DREAMOR_CASE_sumexpr_container_column_not_named
// expect: Name the column: sumExpr<cMachine>
int main() {
  IloEnv env;
  (void)dreamor::concert::sumExpr<std::size_t>(env, I, [](cI) { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_container_column_not_named

#ifdef DREAMOR_CASE_sumexpr_set_given_a_column
// expect: These rows already are an index set
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  (void)dreamor::concert::sumExpr<cI>(env, sIJ, [](cI) { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_set_given_a_column

#ifdef DREAMOR_CASE_sumexpr_plain_tuples
// expect: These rows are tuples of plain values
int main() {
  IloEnv env;
  const std::vector<std::tuple<std::size_t, std::size_t>> rows{{1, 2}};
  (void)dreamor::concert::sumExpr(env, rows, [](cI) { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_plain_tuples

#ifdef DREAMOR_CASE_sumexpr_term_reads_no_column
// expect: This callable reads no columns
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  (void)dreamor::concert::sumExpr(env, sIJ, [] { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_reads_no_column

#ifdef DREAMOR_CASE_sumexpr_term_is_a_function_name
// expect: is an indirect call per row
double one(cI /*i*/) {
  return 1.0;
}

int main() {
  IloEnv env;
  const auto sIJ = IJ();
  (void)dreamor::concert::sumExpr(env, sIJ, one).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_is_a_function_name

#ifdef DREAMOR_CASE_sumexpr_term_takes_a_column_by_reference
// expect: Take each column by value
int main() {
  IloEnv env;
  const auto sIJ = IJ();
  (void)dreamor::concert::sumExpr(env, sIJ, [](cI& /*i*/) { return 1.0; }).build();
}
#endif  // DREAMOR_CASE_sumexpr_term_takes_a_column_by_reference
