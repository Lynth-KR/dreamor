// SPDX-License-Identifier: Apache-2.0
// scenario/backend.hpp - what a scenario is built against, picked when it is compiled (build.sh):
// the lean Concert stub (SCENARIO_STUB), CPLEX Concert, OR-Tools MathOpt (SCENARIO_MATHOPT), or
// HiGHS's own C++ API (SCENARIO_HIGHS); on CPLEX, its Callable Library as well (SCENARIO_CALLABLE).
// kBackend names it; solve() hands a model to the solver and
// starts it with no time to solve, where the modeling-language benchmarks stop the clock, and
// checks what the solver was given. Every solver runs on one thread. With SCENARIO_EXPORT=<path>,
// the model the solver holds is written to <path>.mps once it has started, outside every timer, for
// a run that only checks that the arms build one model (equivalence.py).
//
// Each scenario is built twice a backend (build.sh): SCENARIO_DREAMOR=1 is ./<scenario>_<backend>, the
// dreamor arm alone; 0 is ./<scenario>_<backend>_hand, the hand-written arms, with no dreamor header but
// the logger - so a change to the library leaves them as they are.
#pragma once

#ifndef SCENARIO_DREAMOR
#  error "Build with -DSCENARIO_DREAMOR=1 (the dreamor arm) or 0 (the hand-written arms), as build.sh does."
#endif

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "../common/result.hpp"
#if defined(SCENARIO_MATHOPT)
#  if SCENARIO_DREAMOR
#    include "dreamor/general/third_party_headers.hpp"
#    include "dreamor/mathopt.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#  endif
#  include "ortools/math_opt/cpp/math_opt.h"
#  include "ortools/math_opt/io/mps_converter.h"
#  if SCENARIO_DREAMOR
DREAMOR_END_THIRD_PARTY_HEADERS
#  endif
#elif defined(SCENARIO_HIGHS)
#  if SCENARIO_DREAMOR
#    include "dreamor/highs.hpp"
#  else
#    include <Highs.h>
#  endif
#else
#  include <ilcplex/ilocplex.h>
#  if SCENARIO_DREAMOR
#    include "dreamor/concert.hpp"
#  endif
#endif

// Where SCENARIO_EXPORT asks for the model, or empty.
inline std::string exportPath() {
  const char* path = std::getenv("SCENARIO_EXPORT");
  return path == nullptr ? std::string{} : std::string{path} + ".mps";
}

#if defined(SCENARIO_MATHOPT)
namespace math_opt                    = operations_research::math_opt;
inline constexpr const char* kBackend = "mathopt";

// Hands the model to HiGHS and starts it with no time to solve; checks what the solver was given.
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

// Quiet, one thread, and no time to solve: every arm's Highs takes these before it holds a model.
inline HighsOptions startOptions() {
  HighsOptions options;
  options.output_flag = false;
  options.threads     = 1;
  options.time_limit  = 0;
  return options;
}

// Starts HiGHS with no time to solve on what it holds; checks what it was given.
inline void start(Highs& highs, Result& r) {
  r.measure("solve0", [&] { r.sink += highs.run() == HighsStatus::kOk ? 1 : 0; });
  if (const std::string path = exportPath(); !path.empty()) { highs.writeModel(path); }
  r.check("rows", static_cast<std::uint64_t>(highs.getNumRow()));
  r.check("cols", static_cast<std::uint64_t>(highs.getNumCol()));
  r.check("nonzeros", static_cast<std::uint64_t>(highs.getLp().a_matrix_.numNz()));
}

#  if SCENARIO_DREAMOR
// dreamor: the model reaches HiGHS, in one passModel, when the Highs is first reached.
inline void solve(dreamor::highs::HighsEnv& env, Result& r) {
  Highs* highs = nullptr;
  r.measure("extract", [&] { highs = &env.highs(); });
  start(*highs, r);
}
#  endif

// By hand: the model written into a HighsModel, handed over in one passModel.
inline void solve(Highs& highs, HighsModel model, Result& r) {
  r.measure("extract", [&] { r.sink += highs.passModel(std::move(model)) == HighsStatus::kOk ? 1 : 0; });
  start(highs, r);
}

// By hand, a call a row: the Highs already holds the model.
inline void solve(Highs& highs, Result& r) {
  r.measure("extract", [] {});
  start(highs, r);
}
#elif defined(SCENARIO_STUB)
inline constexpr const char* kBackend = "stub";

// No solver: what the model holds is the check - its rows and nonzeros, the objective's
// coefficients and the constraints' finite bounds, each summed.
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

// CPLEX's Callable Library, the C API under Concert: an environment and a problem, set as solve()
// sets IloCplex - quiet, one thread, no time to solve.
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

// Starts CPLEX with no time to solve on what the problem holds; checks what it was given.
inline void start(const Callable& cpx, Result& r) {
  r.measure("solve0", [&] { r.sink += CPXmipopt(cpx.env(), cpx.lp()) == 0 ? 1 : 0; });
  if (const std::string path = exportPath(); !path.empty()) {
    CPXwriteprob(cpx.env(), cpx.lp(), path.c_str(), nullptr);
  }
  r.check("rows", static_cast<std::uint64_t>(CPXgetnumrows(cpx.env(), cpx.lp())));
  r.check("cols", static_cast<std::uint64_t>(CPXgetnumcols(cpx.env(), cpx.lp())));
  r.check("nonzeros", static_cast<std::uint64_t>(CPXgetnumnz(cpx.env(), cpx.lp())));
}

// Hands the model to CPLEX and starts it with no time to solve; checks what the solver was given.
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
