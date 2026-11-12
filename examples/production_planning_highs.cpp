// SPDX-License-Identifier: MIT-0
//
// Builds the lot-sizing example on the HiGHS backend, solves it with HiGHS and prints the plan.
// tests/highs/CMakeLists.txt builds and runs it.
#include <cstdio>
#include <exception>

#include "production_planning_highs.hpp"

int main() try {
  using namespace planning;
  const Instance in = Instance::make(3, 6, 5);
  HighsOptions options;
  options.output_flag = false;
  dreamor::highs::HighsEnv env{options};
  const highs::Model lotSizing{env, in};
  lotSizing.build(env);

  Highs& highs = env.highs();
  if (highs.run() != HighsStatus::kOk) {
    std::fprintf(stderr, "run: %s\n", highs.modelStatusToString(highs.getModelStatus()).c_str());
    return 1;
  }
  if (highs.getInfo().primal_solution_status != kSolutionStatusFeasible) {
    std::printf("no solution\n");
    return 1;
  }
  std::printf("cost %.1f\n", highs.getInfo().objective_function_value);
  const auto yieldPlan = lotSizing.vYield.solution(env);
  const auto stockPlan = lotSizing.vStock.solution(env);
  for (const auto& [period] : lotSizing.sPERIODS) {
    std::printf("period %d\n", period.get());
    for (const auto& [row, qty] : yieldPlan) {
      const auto& [m, i, t] = row;
      if (t == period && qty > 1e-6) { std::printf("  %s makes %6.1f of %s\n", m->name.c_str(), qty, i->name.c_str()); }
    }
    for (const auto& [row, qty] : stockPlan) {
      const auto& [i, t] = row;
      if (t == period && qty > 1e-6) { std::printf("  %s: %6.1f in stock\n", i->name.c_str(), qty); }
    }
  }
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "error: %s\n", e.what());
  return 1;
} catch (...) {
  std::fprintf(stderr, "error: an exception that is not a std::exception\n");
  return 1;
}
