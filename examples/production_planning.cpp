// SPDX-License-Identifier: Apache-2.0
// Builds and solves the lot-sizing example, then prints the plan.
//   tests/run_tests.sh --cplex builds and runs it; by hand, as one command:
//   g++ -std=c++20 -DIL_STD -Iinclude -isystem $CPLEX/concert/include -isystem $CPLEX/cplex/include
//       examples/production_planning.cpp -L$CPLEX/cplex/lib/x86-64_linux/static_pic
//       -L$CPLEX/concert/lib/x86-64_linux/static_pic -lilocplex -lconcert -lcplex -lm -lpthread -ldl
#include <cstdio>
#include <exception>

#include "production_planning.hpp"

int main() try {
  using namespace planning;
  const Instance in = Instance::make(3, 6, 5);
  IloEnv env;
  try {
    const IloModel model(env);
    const Model lotSizing{env, in};
    lotSizing.build(env, model);

    IloCplex cplex(model);
    cplex.setOut(env.getNullStream());
    if (cplex.solve() == IloFalse) {
      std::printf("no solution\n");
      env.end();
      return 1;
    }
    std::printf("cost %.1f\n", cplex.getObjValue());
    const auto yieldPlan = lotSizing.vYield.solution(cplex);
    const auto stockPlan = lotSizing.vStock.solution(cplex);
    for (const auto& [period] : lotSizing.sPERIODS) {
      std::printf("period %zu\n", period.get());
      for (const auto& [row, qty] : yieldPlan)
        if (std::get<PeriodCol>(row) == period && qty > 1e-6)
          std::printf("  %s makes %6.1f of %s\n",
                      std::get<MachineCol>(row)->name.c_str(),
                      qty,
                      std::get<ItemCol>(row)->name.c_str());
      for (const auto& [row, qty] : stockPlan)
        if (std::get<PeriodCol>(row) == period && qty > 1e-6)
          std::printf("  %s: %6.1f in stock\n", std::get<ItemCol>(row)->name.c_str(), qty);
    }
  } catch (const IloException& e) {
    std::fprintf(stderr, "%s\n", e.getMessage());
  }
  env.end();
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "error: %s\n", e.what());
  return 1;
} catch (...) {
  std::fprintf(stderr, "error: an exception that is not a std::exception\n");
  return 1;
}
