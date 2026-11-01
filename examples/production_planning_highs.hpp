// SPDX-License-Identifier: Apache-2.0
// The lot-sizing model of production_planning_sets.hpp, on the HiGHS backend: the same sets, the
// same constraints as production_planning.hpp, written against HiGHS's own C++ API, in highspy's
// terms, instead of Concert.
#pragma once

#include "dreamor/highs.hpp"
#include "production_planning_sets.hpp"

namespace planning {

struct HighsApiModel : Sets {
  const dreamor::highs::VarTable<MachineCol, ItemCol, PeriodCol> vYield;
  const dreamor::highs::VarTable<MachineCol, ItemCol, PeriodCol> vSetup;
  const dreamor::highs::VarTable<ItemCol, PeriodCol> vStock;

  static constexpr auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };

  HighsApiModel(dreamor::highs::HighsEnv& env, const Instance& in)
      : Sets{in},
        vYield{env, sPRODUCE, 0, max_possible_qty, HighsVarType::kContinuous, "Yield"},
        vSetup{env, sPRODUCE, 0, 1, HighsVarType::kInteger, "Setup"},
        vStock{env, sSTOCK, 0, kHighsInf, HighsVarType::kContinuous, "Stock"} {}

  void build(dreamor::highs::HighsEnv& env) const {
    using dreamor::highs::addConstr;
    using dreamor::highs::HighsLinearExpression;
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };

    // balance: stock carried in + made - demand = stock carried out
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
    for (const auto& [i, t] : sSTOCK) {
      HighsLinearExpression totalInFlow = dreamor::highs::sumOf(producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) {
        totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
      }
      const double currentDemand = i->demand[t.get()];
      const auto endingStock     = vStock(i, t);
      addConstr(env, totalInFlow - currentDemand == endingStock);
    }

    // capacity of each machine in each period
    const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      addConstr(env, dreamor::highs::sumOf(rows, machine_usage).build() <= machine->capacity);
    }

    // no production without a setup; a HighsVar is an index, so the bound comes from the row
    for (const auto& [row, yieldVar] : vYield) {
      const double upper = max_possible_qty(std::get<MachineCol>(row), std::get<ItemCol>(row));
      addConstr(env, yieldVar <= upper * vSetup(row));
    }

    const HighsLinearExpression holdingCost =
        dreamor::highs::sumOf(sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
    const HighsLinearExpression setupCost = dreamor::highs::sumOf(sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) {
                                              return m->setup * vSetup(m, i, t);
                                            }).build();
    dreamor::highs::setObjective(env, holdingCost + setupCost, ObjSense::kMinimize);
  }
};

}  // namespace planning
