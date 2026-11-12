// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Against the real CPLEX (the cplex preset): the library model and the plain-Concert
// model of examples/ must have the same rows, columns, nonzeros and optimum, and the
// library's solution must satisfy the stock balance.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include <ilcplex/ilocplex.h>

#include "production_planning_concert.hpp"
#include "production_planning_plain.hpp"
#include "support/check.hpp"

using namespace planning;

namespace {

struct Result {
  IloInt rows, cols, nnz;
  double objective;
  bool optimal;
};

}  // namespace

static Result solve(const IloModel& model) {
  IloCplex cplex(model);
  cplex.setOut(model.getEnv().getNullStream());
  const bool ok = cplex.solve() != IloFalse && cplex.getStatus() == IloAlgorithm::Optimal;
  Result r{cplex.getNrows(), cplex.getNcols(), cplex.getNNZs(), ok ? cplex.getObjValue() : 0.0, ok};
  cplex.end();
  return r;
}

int main() try {
  for (auto [M, I, T] : {std::array<int, 3>{2, 3, 4}, std::array<int, 3>{3, 6, 5}}) {
    const Instance in = Instance::make(M, I, T);
    Result lib{};
    Result plain{};
    {
      IloEnv env;
      try {
        const IloModel model(env);
        const concert::Model m{env, in};
        m.build(env, model);
        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());
        DREAMOR_CHECK(cplex.solve());
        lib = Result{
            cplex.getNrows(),
            cplex.getNcols(),
            cplex.getNNZs(),
            cplex.getObjValue(),
            cplex.getStatus() == IloAlgorithm::Optimal,
        };
        const auto yieldPlan = m.vYield.solution(cplex);
        const auto stockPlan = m.vStock.solution(cplex);

        const auto producedByItemPeriod = m.sPRODUCE.groupBy<cItem, cPeriod>();
        for (const auto& [i, t] : m.sSTOCK) {
          double netFlow = (t > 0 ? stockPlan(i, t - 1) : 0.0) - i->demand[static_cast<std::size_t>(t.get())];
          for (const auto& row : producedByItemPeriod.slice(i, t)) {
            netFlow += yieldPlan(row);
          }
          DREAMOR_CHECK(std::abs(netFlow - stockPlan(i, t)) < 1e-6);
        }
        cplex.end();
      } catch (const IloException& e) {
        std::fprintf(stderr, "library IloException: %s\n", e.getMessage());
        ++g_failures;
      }
      env.end();
    }
    {
      IloEnv env;
      try {
        const IloModel model(env);
        PlainModel p{env, in};
        p.build(env, model, in);
        plain = solve(model);
      } catch (const IloException& e) {
        std::fprintf(stderr, "plain IloException: %s\n", e.getMessage());
        ++g_failures;
      }
      env.end();
    }
    DREAMOR_CHECK(lib.optimal && plain.optimal);
    DREAMOR_CHECK(lib.rows == plain.rows && lib.cols == plain.cols && lib.nnz == plain.nnz);
    DREAMOR_CHECK(std::abs(lib.objective - plain.objective) < 1e-6 * (1 + std::abs(plain.objective)));
  }
  return report("cplex_example");
} catch (...) { return ::uncaught(); }
