// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Builds the lot-sizing example on the HiGHS backend, solves it with HiGHS and prints the plan.
// tests/run_tests.sh --highs [HiGHS dir] builds and runs it.
#include <cstdio>
#include <exception>

#include "production_planning_highs.hpp"

int main() try {
  using namespace planning;
  const Instance in = Instance::make(3, 6, 5);
  HighsOptions options;
  options.output_flag = false;
  dreamor::highs::HighsEnv env{options};
  const HighsApiModel lotSizing{env, in};
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
    std::printf("period %zu\n", period.get());
    for (const auto& [row, qty] : yieldPlan) {
      if (std::get<cPeriod>(row) == period && qty > 1e-6) {
        std::printf(
            "  %s makes %6.1f of %s\n", std::get<cMachine>(row)->name.c_str(), qty, std::get<cItem>(row)->name.c_str());
      }
    }
    for (const auto& [row, qty] : stockPlan) {
      if (std::get<cPeriod>(row) == period && qty > 1e-6) {
        std::printf("  %s: %6.1f in stock\n", std::get<cItem>(row)->name.c_str(), qty);
      }
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
