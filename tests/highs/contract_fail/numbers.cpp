// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Code that breaks a contract, for a debug build to stop; a case's form is docs/dev/diagnostics.md's. Each case
// divides by a zero that the compiler cannot see, as a model divides by a datum that is 0.
#include "prelude.hpp"
// The headers its cases use, beside the prelude's.
#include <cstdlib>

#ifdef DREAMOR_CASE_row_coefficient_infinite
// expect: a coefficient of the row is not finite
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  return hs::addConstr(env, x(cI{1}) / zero <= 1).index();
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_row_coefficient_infinite

#ifdef DREAMOR_CASE_row_coefficient_nan
// expect: a coefficient of the row is not finite
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  return hs::addConstr(env, (zero / zero) * x(cI{1}) <= 1).index();
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_row_coefficient_nan

#ifdef DREAMOR_CASE_row_bound_nan
// expect: a bound of the row is NaN
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  return hs::addConstr(env, x(cI{1}) <= zero / zero).index();
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_row_bound_nan

#ifdef DREAMOR_CASE_objective_coefficient_infinite
// expect: a coefficient or the constant of the objective is not finite
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  return static_cast<int>(hs::minimize(env, x(cI{1}) / zero));
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_objective_coefficient_infinite

#ifdef DREAMOR_CASE_objective_coefficient_nan
// expect: a coefficient or the constant of the objective is not finite
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  hs::setObjective(env, (zero / zero) * x(cI{1}));
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_objective_coefficient_nan

#ifdef DREAMOR_CASE_objective_constant_infinite
// expect: a coefficient or the constant of the objective is not finite
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  hs::setObjective(env, x(cI{1}) + 1 / zero);
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_objective_constant_infinite

#ifdef DREAMOR_CASE_objective_quadratic_infinite
// expect: a coefficient or the constant of the objective is not finite
// contract: highs_finite_numbers
int main() try {
  hs::HighsEnv env;
  const hs::VarTable<cI> x{env, sI(), 0, 1};
  const double zero = std::strtod("0", nullptr);
  hs::setObjective(env, (x(cI{1}) * x(cI{1})) / zero);
  return 0;
} catch (...) {
  return 1;  // no message: the runner reports it
}
#endif  // DREAMOR_CASE_objective_quadratic_infinite
