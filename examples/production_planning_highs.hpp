// SPDX-License-Identifier: MIT-0
//
// The lot-sizing model of production_planning_sets.hpp, on the HiGHS backend: the same sets, the
// same constraints as production_planning_concert.hpp, written against HiGHS's own C++ API, in highspy's
// terms, instead of Concert.
#pragma once

#include "dreamor/highs.hpp"
#include "production_planning_sets.hpp"

namespace planning::highs {
using namespace dreamor::highs;

struct Model : Sets {
  const VarTable<cMachine, cItem, cPeriod> vYield;
  const VarTable<cMachine, cItem, cPeriod> vSetup;
  const VarTable<cItem, cPeriod> vStock;

  static constexpr auto max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; };

  Model(HighsEnv& env, const Instance& in)
      : Sets{in},
        vYield{env, sPRODUCE, 0, max_possible_qty, HighsVarType::kContinuous, "Yield"},
        vSetup{env, sPRODUCE, 0, 1, HighsVarType::kInteger, "Setup"},
        vStock{env, sSTOCK, 0, kHighsInf, HighsVarType::kContinuous, "Stock"} {}

  void build(HighsEnv& env) const {
    const auto machine_usage = [&](cMachine m, cItem i, cPeriod t) { return i->time * vYield(m, i, t); };

    // balance: stock carried in + made - demand = stock carried out
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();
    for (const auto& [i, t] : sSTOCK) {
      HighsLinearExpression totalInFlow = sumExpr(producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) { totalInFlow += vStock(i, *prevPeriod); }
      const double currentDemand = i->demand[t];
      const auto endingStock     = vStock(i, t);
      addConstr(env, totalInFlow - currentDemand == endingStock);
    }

    // capacity of each machine in each period
    const auto producedByMachinePeriod = sPRODUCE.groupBy<cMachine, cPeriod>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      addConstr(env, sumExpr(rows, machine_usage).build() <= machine->capacity);
    }

    // no production without a setup; a HighsVar is an index, so the bound comes from the row
    for (const auto& [row, yieldVar] : vYield) {
      const auto& [m, i, t] = row;
      const double upper    = max_possible_qty(m, i);
      addConstr(env, yieldVar <= upper * vSetup(row));
    }

    const auto holding_term = [&](cItem i, cPeriod t) { return i->hold * vStock(i, t); };
    const auto setup_term   = [&](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
    const HighsLinearExpression holdingCost = sumExpr(sSTOCK, holding_term).build();
    const HighsLinearExpression setupCost   = sumExpr(sPRODUCE, setup_term).build();
    setObjective(env, holdingCost + setupCost, ObjSense::kMinimize);
  }
};

}  // namespace planning::highs
