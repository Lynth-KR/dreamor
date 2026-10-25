// SPDX-License-Identifier: Apache-2.0
// scenario/backend.hpp - what a scenario is built against, picked when it is compiled (build.sh):
// the lean Concert stub (SCENARIO_STUB), CPLEX Concert, or OR-Tools MathOpt (SCENARIO_MATHOPT).
// kBackend names it; solve() hands a model to the solver and starts it with no time to solve,
// where the modeling-language benchmarks stop the clock, and checks what the solver was given.
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>

#include "../common/result.hpp"
#if defined(SCENARIO_MATHOPT)
#  include "dreamor/general/third_party_headers.hpp"
#  include "dreamor/mathopt.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#  include "ortools/math_opt/cpp/math_opt.h"
DREAMOR_END_THIRD_PARTY_HEADERS
#else
#  include <ilcplex/ilocplex.h>
#  include "dreamor/concert.hpp"
#endif

#if defined(SCENARIO_MATHOPT)
namespace math_opt                    = operations_research::math_opt;
inline constexpr const char* kBackend = "mathopt";

// Hands the model to HiGHS and starts it with no time to solve; checks what the solver was given.
inline void solve(math_opt::Model& model, Result& r) {
  std::unique_ptr<math_opt::IncrementalSolver> solver;
  r.measure("extract", [&] { solver = math_opt::NewIncrementalSolver(&model, math_opt::SolverType::kHighs).value(); });
  math_opt::SolveArguments args;
  args.parameters.time_limit = absl::ZeroDuration();
  r.measure("solve0", [&] { r.sink += solver->Solve(args).ok(); });
  std::uint64_t nonzeros = 0;
  for (const math_opt::LinearConstraint& c : model.LinearConstraints()) nonzeros += model.RowNonzeros(c).size();
  r.check("rows", model.num_linear_constraints());
  r.check("cols", model.num_variables());
  r.check("nonzeros", nonzeros);
}
#elif defined(SCENARIO_STUB)
inline constexpr const char* kBackend = "stub";

// No solver: what the model holds is the check - its rows and nonzeros, the objective's
// coefficients and the constraints' finite bounds, each summed.
inline void solve(IloEnv /*env*/, const IloModel& model, Result& r) {
  double coefficients = 0, bounds = 0;
  for (const double coef : model.objective()->coefs) coefficients += coef;
  for (const IloConstraintI* c : model.constraints()) {
    if (c->lb > -IloInfinity) bounds += c->lb;
    if (c->ub < IloInfinity) bounds += c->ub;
  }
  r.check("rows", model.rows());
  r.check("nonzeros", model.nonzeros());
  r.check("objective", static_cast<std::uint64_t>(std::llround(coefficients)));
  r.check("bounds", static_cast<std::uint64_t>(std::llround(bounds)));
}
#else
inline constexpr const char* kBackend = "cplex";

// Hands the model to CPLEX and starts it with no time to solve; checks what the solver was given.
inline void solve(IloEnv env, const IloModel& model, Result& r) {
  IloCplex cplex(env);
  cplex.setOut(env.getNullStream());
  cplex.setWarning(env.getNullStream());
  cplex.setParam(IloCplex::Param::Threads, 1);
  cplex.setParam(IloCplex::Param::TimeLimit, 0);
  r.measure("extract", [&] { cplex.extract(model); });
  r.measure("solve0", [&] { r.sink += cplex.solve() ? 1 : 0; });
  r.check("rows", static_cast<std::uint64_t>(cplex.getNrows()));
  r.check("cols", static_cast<std::uint64_t>(cplex.getNcols()));
  r.check("nonzeros", static_cast<std::uint64_t>(cplex.getNNZs()));
}
#endif
