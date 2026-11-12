// SPDX-License-Identifier: MIT-0
//
// Builds the lot-sizing example on the MathOpt backend, solves it with HiGHS and prints the
// plan. tests/mathopt/CMakeLists.txt builds and runs it.
#include <cstdio>
#include <exception>
#include <iostream>

#include "ortools/math_opt/cpp/math_opt.h"
#include "production_planning_mathopt.hpp"

int main() try {
  namespace math_opt = ::operations_research::math_opt;
  using namespace planning;
  const Instance in = Instance::make(3, 6, 5);
  math_opt::Model model("lot sizing");
  const mathopt::Model lotSizing{model, in};
  lotSizing.build(model);

  const auto result = math_opt::Solve(model, math_opt::SolverType::kHighs);
  if (!result.ok()) {
    std::cerr << result.status() << "\n";
    return 1;
  }
  if (!result->has_primal_feasible_solution()) {
    std::printf("no solution\n");
    return 1;
  }
  std::printf("cost %.1f\n", result->objective_value());
  const auto yieldPlan = lotSizing.vYield.solution(*result);
  const auto stockPlan = lotSizing.vStock.solution(*result);
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
