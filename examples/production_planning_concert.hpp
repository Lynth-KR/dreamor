// SPDX-License-Identifier: MIT-0
//
// The lot-sizing model of production_planning_sets.hpp, on the Concert backend (CPLEX).
#pragma once

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "production_planning_sets.hpp"

namespace planning::concert {
using namespace dreamor::concert;

struct Model : Sets {
  const VarTable<cMachine, cItem, cPeriod> vYield;
  const VarTable<cMachine, cItem, cPeriod> vSetup;
  const VarTable<cItem, cPeriod> vStock;

  static constexpr auto max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; };

  Model(IloEnv env, const Instance& in)
      : Sets{in},
        vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"},
        vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"},
        vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"} {}

  void build(IloEnv env, const IloModel& model) const {
    const auto machine_usage = [&](cMachine m, cItem i, cPeriod t) { return i->time * vYield(m, i, t); };

    // balance: stock carried in + made - demand = stock carried out
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();
    for (const auto& [i, t] : sSTOCK) {
      IloExpr totalInFlow = sumExpr(env, producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) { totalInFlow += vStock(i, *prevPeriod); }
      const double currentDemand = i->demand[t];
      const auto endingStock     = vStock(i, t);
      model.add(totalInFlow - currentDemand == endingStock);
      totalInFlow.end();
    }

    // capacity of each machine in each period
    const auto producedByMachinePeriod = sPRODUCE.groupBy<cMachine, cPeriod>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      IloExpr totalLoad = sumExpr(env, rows, machine_usage).build();
      model.add(totalLoad <= machine->capacity);
      totalLoad.end();
    }

    // no production without a setup
    for (const auto& [row, yieldVar] : vYield) {
      model.add(yieldVar <= yieldVar.getUB() * vSetup(row));
    }

    const auto holding_term = [&](cItem i, cPeriod t) { return i->hold * vStock(i, t); };
    const auto setup_term   = [&](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
    IloExpr holdingCost     = sumExpr(env, sSTOCK, holding_term).build();
    IloExpr setupCost       = sumExpr(env, sPRODUCE, setup_term).build();
    IloExpr totalCost       = holdingCost + setupCost;
    model.add(IloMinimize(env, totalCost));
    holdingCost.end();
    setupCost.end();
    totalCost.end();
  }
};
}  // namespace planning::concert
