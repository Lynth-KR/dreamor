// SPDX-License-Identifier: Apache-2.0
// The lot-sizing model of production_planning_sets.hpp, on the MathOpt backend: the same
// sets, the same constraints as production_planning.hpp, written against MathOpt instead of
// Concert. Solvable by any MathOpt MIP solver (HiGHS, SCIP, Gurobi, Xpress, ...).
#pragma once

#include <limits>

#include "dreamor/mathopt.hpp"
#include "production_planning_sets.hpp"

namespace planning {

struct MathOptModel : Sets {
  const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vYield;
  const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vSetup;
  const dreamor::mathopt::VarTable<ItemCol, PeriodCol> vStock;

  static constexpr auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };

  MathOptModel(operations_research::math_opt::Model& model, const Instance& in)
      : Sets{in},
        vYield{model, sPRODUCE, 0, max_possible_qty, /*is_integer=*/false, "Yield"},
        vSetup{model, sPRODUCE, 0, 1, /*is_integer=*/true, "Setup"},
        vStock{model, sSTOCK, 0, std::numeric_limits<double>::infinity(), /*is_integer=*/false, "Stock"} {}

  void build(operations_research::math_opt::Model& model) const {
    using operations_research::math_opt::LinearExpression;
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };

    // balance: stock carried in + made - demand = stock carried out
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
    for (const auto& [i, t] : sSTOCK) {
      LinearExpression totalInFlow = dreamor::mathopt::sumOf(producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) {
        totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
      }
      const double currentDemand = i->demand[t.get()];
      const auto endingStock     = vStock(i, t);
      model.AddLinearConstraint(totalInFlow - currentDemand == endingStock);
    }

    // capacity of each machine in each period
    const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      const LinearExpression totalLoad = dreamor::mathopt::sumOf(rows, machine_usage).build();
      model.AddLinearConstraint(totalLoad <= machine->capacity);
    }

    // no production without a setup
    for (const auto& [row, yieldVar] : vYield) {
      model.AddLinearConstraint(yieldVar <= yieldVar.upper_bound() * vSetup(row));
    }

    const LinearExpression holdingCost =
        dreamor::mathopt::sumOf(sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
    const LinearExpression setupCost = dreamor::mathopt::sumOf(sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) {
                                         return m->setup * vSetup(m, i, t);
                                       }).build();
    model.Minimize(holdingCost + setupCost);
  }
};

}  // namespace planning
