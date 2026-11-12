// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/backend.hpp - the backend that a scenario is built against, which build.sh picks at compile time.
//
// The backend is one of these:
//   - the lean Concert stub, if SCENARIO_STUB is defined;
//   - OR-Tools MathOpt, if SCENARIO_MATHOPT is defined;
//   - HiGHS's own C++ API, if SCENARIO_HIGHS is defined;
//   - CPLEX Concert, if none of the three is defined. CPLEX's Callable Library is then a backend as well, and this
//     header defines SCENARIO_CALLABLE for it.
// kBackend names the backend. solve() hands a model to the solver, starts the solver with no time to solve, and checks
// what the solver was given. The modeling-language benchmarks stop the clock at that start. Every solver runs on one
// thread. If the environment variable SCENARIO_EXPORT holds a path, the model that the solver holds is written to
// <path>.mps after the solver has started, outside every timer. equivalence.py sets it, to check that the arms build
// one model.
//
// Both arms of a scenario include this header, so it includes no dreamor header. build.sh builds each scenario twice
// for each backend:
//   - <scenario>_dreamor.cpp as ./<scenario>_<backend>, the dreamor arm alone, which includes backend_dreamor.hpp;
//   - <scenario>_hand.cpp as ./<scenario>_<backend>_hand, the hand-written arms, without dreamor's include path.
// So a change to the library leaves the hand-written arms as they are.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "../common/result.hpp"
#if defined(SCENARIO_MATHOPT)
#  include "ortools/math_opt/cpp/math_opt.h"
#  include "ortools/math_opt/io/mps_converter.h"
#elif defined(SCENARIO_HIGHS)
#  include <Highs.h>
#else
#  include <ilcplex/ilocplex.h>
#endif

// Returns the file that SCENARIO_EXPORT asks the model to be written to, or an empty string if it is not set.
inline std::string exportPath() {
  const char* path = std::getenv("SCENARIO_EXPORT");
  return path == nullptr ? std::string{} : std::string{path} + ".mps";
}

#if defined(SCENARIO_MATHOPT)
namespace math_opt                    = operations_research::math_opt;
inline constexpr const char* kBackend = "mathopt";

// Hands the model to HiGHS, starts HiGHS with no time to solve, and checks what HiGHS was given.
inline void solve(math_opt::Model& model, Result& r) {
  std::unique_ptr<math_opt::IncrementalSolver> solver;
  r.measure("extract", [&] { solver = math_opt::NewIncrementalSolver(&model, math_opt::SolverType::kHighs).value(); });
  math_opt::SolveArguments args;
  args.parameters.time_limit = absl::ZeroDuration();
  args.parameters.threads    = 1;
  r.measure("solve0", [&] { r.sink += solver->Solve(args).ok(); });
  if (const std::string path = exportPath(); !path.empty()) {
    std::ofstream(path) << math_opt::ModelProtoToMps(model.ExportModel()).value();
  }
  std::uint64_t nonzeros = 0;
  for (const math_opt::LinearConstraint& c : model.LinearConstraints()) {
    nonzeros += model.RowNonzeros(c).size();
  }
  r.check("rows", model.num_linear_constraints());
  r.check("cols", model.num_variables());
  r.check("nonzeros", nonzeros);
}
#elif defined(SCENARIO_HIGHS)
inline constexpr const char* kBackend = "highs";

// Returns the options that every arm's Highs takes before it holds a model: no output, one thread, and no time to
// solve.
inline HighsOptions startOptions() {
  HighsOptions options;
  options.output_flag = false;
  options.threads     = 1;
  options.time_limit  = 0;
  return options;
}

// Starts HiGHS with no time to solve on the model that it holds, and checks what it was given.
inline void start(Highs& highs, Result& r) {
  r.measure("solve0", [&] { r.sink += highs.run() == HighsStatus::kOk ? 1 : 0; });
  if (const std::string path = exportPath(); !path.empty()) { highs.writeModel(path); }
  r.check("rows", static_cast<std::uint64_t>(highs.getNumRow()));
  r.check("cols", static_cast<std::uint64_t>(highs.getNumCol()));
  r.check("nonzeros", static_cast<std::uint64_t>(highs.getLp().a_matrix_.numNz()));
}

// Hands over a model that an arm wrote into a HighsModel, in one passModel, and starts HiGHS.
inline void solve(Highs& highs, HighsModel model, Result& r) {
  r.measure("extract", [&] { r.sink += highs.passModel(std::move(model)) == HighsStatus::kOk ? 1 : 0; });
  start(highs, r);
}

// Starts HiGHS on a model that an arm handed over a row at a time, so the Highs already holds it.
inline void solve(Highs& highs, Result& r) {
  r.measure("extract", [] {});
  start(highs, r);
}
#elif defined(SCENARIO_STUB)
inline constexpr const char* kBackend = "stub";

// The stub has no solver, so the checks are what the model holds: its rows, its nonzeros, the sum of the objective's
// coefficients, and the sum of the constraints' finite bounds.
inline void solve(IloEnv /*env*/, const IloModel& model, Result& r) {
  double coefficients = 0, bounds = 0;
  for (const double coef : model.objective()->coefs) {
    coefficients += coef;
  }
  for (const IloConstraintI* c : model.constraints()) {
    if (c->lb > -IloInfinity) { bounds += c->lb; }
    if (c->ub < IloInfinity) { bounds += c->ub; }
  }
  r.check("rows", model.rows());
  r.check("nonzeros", model.nonzeros());
  r.check("objective", static_cast<std::uint64_t>(std::llround(coefficients)));
  r.check("bounds", static_cast<std::uint64_t>(std::llround(bounds)));
}
#else
inline constexpr const char* kBackend = "cplex";
#  define SCENARIO_CALLABLE 1

// CPLEX's Callable Library, the C API under Concert: an environment and a problem. The environment takes the
// settings that solve() gives IloCplex: no output, one thread, and no time to solve.
class Callable {
 public:
  Callable() {
    int status = 0;
    env_       = CPXopenCPLEX(&status);
    CPXsetintparam(env_, CPXPARAM_ScreenOutput, CPX_OFF);
    CPXsetintparam(env_, CPXPARAM_Threads, 1);
    CPXsetdblparam(env_, CPXPARAM_TimeLimit, 0);
    lp_ = CPXcreateprob(env_, &status, "scenario");
  }

  Callable(const Callable&)            = delete;
  Callable& operator=(const Callable&) = delete;

  ~Callable() {
    CPXfreeprob(env_, &lp_);
    CPXcloseCPLEX(&env_);
  }

  [[nodiscard]] CPXENVptr env() const { return env_; }

  [[nodiscard]] CPXLPptr lp() const { return lp_; }

 private:
  CPXENVptr env_ = nullptr;
  CPXLPptr lp_   = nullptr;
};

// Starts CPLEX with no time to solve on the model that the problem holds, and checks what CPLEX was given.
inline void start(const Callable& cpx, Result& r) {
  r.measure("solve0", [&] { r.sink += CPXmipopt(cpx.env(), cpx.lp()) == 0 ? 1 : 0; });
  if (const std::string path = exportPath(); !path.empty()) {
    CPXwriteprob(cpx.env(), cpx.lp(), path.c_str(), nullptr);
  }
  r.check("rows", static_cast<std::uint64_t>(CPXgetnumrows(cpx.env(), cpx.lp())));
  r.check("cols", static_cast<std::uint64_t>(CPXgetnumcols(cpx.env(), cpx.lp())));
  r.check("nonzeros", static_cast<std::uint64_t>(CPXgetnumnz(cpx.env(), cpx.lp())));
}

// Hands the model to CPLEX, starts CPLEX with no time to solve, and checks what CPLEX was given.
inline void solve(IloEnv env, const IloModel& model, Result& r) {
  IloCplex cplex(env);
  cplex.setOut(env.getNullStream());
  cplex.setWarning(env.getNullStream());
  cplex.setParam(IloCplex::Param::Threads, 1);
  cplex.setParam(IloCplex::Param::TimeLimit, 0);
  r.measure("extract", [&] { cplex.extract(model); });
  r.measure("solve0", [&] { r.sink += cplex.solve() ? 1 : 0; });
  if (const std::string path = exportPath(); !path.empty()) { cplex.exportModel(path.c_str()); }
  r.check("rows", static_cast<std::uint64_t>(cplex.getNrows()));
  r.check("cols", static_cast<std::uint64_t>(cplex.getNcols()));
  r.check("nonzeros", static_cast<std::uint64_t>(cplex.getNNZs()));
}
#endif
