// SPDX-License-Identifier: Apache-2.0
// The lot-sizing model of production_planning_sets.hpp, on the Concert backend (CPLEX).
#pragma once

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "production_planning_sets.hpp"

namespace planning {

struct Model : Sets {
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield;
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vSetup;
  const dreamor::concert::VarTable<ItemCol, PeriodCol> vStock;

  static constexpr auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };

  Model(IloEnv env, const Instance& in)
      : Sets{in},
        vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"},
        vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"},
        vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"} {}

  void build(IloEnv env, IloModel model) const {
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };

    // balance: stock carried in + made - demand = stock carried out
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
    for (const auto& [i, t] : sSTOCK) {
      IloExpr totalInFlow = dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
      const double currentDemand = i->demand[t.get()];
      const auto endingStock     = vStock(i, t);
      model.add(totalInFlow - currentDemand == endingStock);
      totalInFlow.end();
    }

    // capacity of each machine in each period
    const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
      model.add(totalLoad <= machine->capacity);
      totalLoad.end();
    }

    // no production without a setup
    for (const auto& [row, yieldVar] : vYield) model.add(yieldVar <= yieldVar.getUB() * vSetup(row));

    IloExpr holdingCost =
        dreamor::concert::sumOf(env, sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
    IloExpr setupCost = dreamor::concert::sumOf(env, sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) {
                          return m->setup * vSetup(m, i, t);
                        }).build();
    IloExpr totalCost = holdingCost + setupCost;
    model.add(IloMinimize(env, totalCost));
    holdingCost.end();
    setupCost.end();
    totalCost.end();
  }
};
}  // namespace planning
