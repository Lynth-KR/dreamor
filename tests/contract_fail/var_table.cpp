// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's. A VarTable's
// contracts are every backend's (VarTableCore); the Concert stub stands in for them.
#include "dreamor/concert.hpp"
#include "prelude.hpp"

#ifdef DREAMOR_CASE_var_table_assigned_over_a_family
// expect: VarTable is assigned over a family of 3 variables - a broken contract: a table is filled once
// contract: table_filled_once
int main() try {
  const IloEnv env;
  const std::vector<std::size_t> values{1, 2, 3};
  const auto sI = dreamor::index_set::relational::from<cI>(values).build();
  dreamor::concert::VarTable<cI> x{env, sI, 0, 1, ILOFLOAT, "x"};
  x = dreamor::concert::VarTable<cI>{env, sI, 0, 2, ILOFLOAT, "y"};  // x holds its family already
  return static_cast<int>(x.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_var_table_assigned_over_a_family
