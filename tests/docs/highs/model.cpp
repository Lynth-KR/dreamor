// SPDX-License-Identifier: MIT-0
//
// The guide's model whole, on HiGHS (plan.hpp): a multi-level lot sizing over the bill of materials. Its sets written
// as chains and in set-builder notation (model.hpp) are the same, and the plan on either solves to the cost of its
// solution worked out by hand.
#include <cmath>

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "docs/highs/plan.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

// The plan solved, its cost.
double solve(const Sets& s, const BomTable& quantity) {
  HighsOptions quiet;
  quiet.output_flag = false;
  highs::Plan plan{s, quantity, quiet};
  plan.env.highs().run();
  DREAMOR_CHECK(plan.env.highs().getModelStatus() == HighsModelStatus::kOptimal);
  return plan.env.highs().getInfo().objective_function_value;
}

void model() {
  const Plant p           = plant();
  const BomTable quantity = bomTable(p);
  const Sets chain        = byChain(p, quantity);
  const Sets setBuilder   = bySetBuilder(p, quantity);

  DREAMOR_CHECK(chain.NEEDED == setBuilder.NEEDED);
  DREAMOR_CHECK(chain.NEEDED.size() == 4 && !chain.NEEDED.contains(p.items.at("bracket")));
  DREAMOR_CHECK(dreamor::transitiveClosure(quantity.keys()).size() == 8);  // frame -> bolt once, by two paths
  DREAMOR_CHECK(chain.PERIODS == setBuilder.PERIODS);
  DREAMOR_CHECK(chain.PRODUCE == setBuilder.PRODUCE);
  DREAMOR_CHECK(chain.STOCK == setBuilder.STOCK);
  DREAMOR_CHECK(chain.CONSUME == setBuilder.CONSUME);
  DREAMOR_CHECK(chain.CONSUME.size() == 18);  // a period: frame's 2 lines on M1, plate's 2 on M1 and on M2

  // The plan HiGHS finds, costed by hand: setups 70 (M1 frame in each period, M1 plate in 1, M2 plate in 3, bolt in
  // each period, nut in 1 and 2) and holding 5.4 (4 plates after period 1, 6 bolts and 8 nuts after period 2).
  DREAMOR_CHECK(std::abs(solve(chain, quantity) - 75.4) < 1e-6);
  DREAMOR_CHECK(std::abs(solve(setBuilder, quantity) - 75.4) < 1e-6);
}

}  // namespace

int main() try {
  model();
  return ::report("guide/model");
} catch (...) { return ::uncaught(); }
