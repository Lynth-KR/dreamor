// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <utility>

#ifdef DREAMOR_CASE_expression_mixes_models
// expect: one expression holds variables of two HighsEnvs
// contract: highs_one_environment
int main() try {
  hs::HighsEnv first;
  hs::HighsEnv second;
  const auto set = sI();
  const hs::VarTable<cI> x{first, set, 0, 1};
  const hs::VarTable<cI> y{second, set, 0, 1};
  const hs::HighsLinearExpression mixed = x(cI{1}) + y(cI{1});
  return static_cast<int>(mixed.size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_expression_mixes_models

#ifdef DREAMOR_CASE_row_of_another_model
// expect: the expression's variables belong to another HighsEnv
// contract: highs_expression_of_its_environment
int main() try {
  hs::HighsEnv first;
  hs::HighsEnv second;
  const auto set = sI();
  const hs::VarTable<cI> x{first, set, 0, 1};
  return hs::addConstr(second, x(cI{1}) <= 1).index();
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_row_of_another_model

#ifdef DREAMOR_CASE_product_mixes_models
// expect: one expression holds variables of two HighsEnvs
// contract: highs_one_environment
int main() try {
  hs::HighsEnv first;
  hs::HighsEnv second;
  const auto set = sI();
  const hs::VarTable<cI> x{first, set, 0, 1};
  const hs::VarTable<cI> y{second, set, 0, 1};
  const hs::HighsLinearExpression left     = x(cI{1}) + 1.0;
  const hs::HighsLinearExpression right    = y(cI{1}) + 1.0;
  const hs::HighsQuadraticExpression mixed = left * right;
  return static_cast<int>(mixed.linear().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_product_mixes_models

#ifdef DREAMOR_CASE_moved_expression_mixes_models
// expect: one expression holds variables of two HighsEnvs
// contract: highs_one_environment
int main() try {
  hs::HighsEnv first;
  hs::HighsEnv second;
  const auto set = sI();
  const hs::VarTable<cI> x{first, set, 0, 1};
  const hs::VarTable<cI> y{second, set, 0, 1};
  hs::HighsQuadraticExpression quadratic = x(cI{1}) * x(cI{1});  // its linear part has first's model and no term
  hs::HighsLinearExpression linear       = y(cI{1});
  quadratic += std::move(linear);
  return static_cast<int>(quadratic.linear().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_moved_expression_mixes_models
