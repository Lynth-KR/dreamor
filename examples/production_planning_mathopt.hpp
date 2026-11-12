// SPDX-License-Identifier: MIT-0
//
// The lot-sizing model of production_planning_sets.hpp, on the MathOpt backend: the same
// sets, the same constraints as production_planning_concert.hpp, written against MathOpt instead of
// Concert. Solvable by any MathOpt MIP solver (HiGHS, SCIP, Gurobi, Xpress, ...).
#pragma once

#include <limits>

#include "dreamor/mathopt.hpp"
#include "production_planning_sets.hpp"

namespace planning::mathopt {
using namespace dreamor::mathopt;

struct Model : Sets {
  const VarTable<cMachine, cItem, cPeriod> vYield;
  const VarTable<cMachine, cItem, cPeriod> vSetup;
  const VarTable<cItem, cPeriod> vStock;

  static constexpr auto max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; };

  Model(operations_research::math_opt::Model& model, const Instance& in)
      : Sets{in},
        vYield{model, sPRODUCE, 0, max_possible_qty, /*is_integer=*/false, "Yield"},
        vSetup{model, sPRODUCE, 0, 1, /*is_integer=*/true, "Setup"},
        vStock{model, sSTOCK, 0, std::numeric_limits<double>::infinity(), /*is_integer=*/false, "Stock"} {}

  void build(operations_research::math_opt::Model& model) const {
    using operations_research::math_opt::LinearExpression;
    const auto machine_usage = [&](cMachine m, cItem i, cPeriod t) { return i->time * vYield(m, i, t); };

    // balance: stock carried in + made - demand = stock carried out
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();
    for (const auto& [i, t] : sSTOCK) {
      LinearExpression totalInFlow = sumExpr(producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) { totalInFlow += vStock(i, *prevPeriod); }
      const double currentDemand = i->demand[t];
      const auto endingStock     = vStock(i, t);
      model.AddLinearConstraint(totalInFlow - currentDemand == endingStock);
    }

    // capacity of each machine in each period
    const auto producedByMachinePeriod = sPRODUCE.groupBy<cMachine, cPeriod>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      const LinearExpression totalLoad = sumExpr(rows, machine_usage).build();
      model.AddLinearConstraint(totalLoad <= machine->capacity);
    }

    // no production without a setup
    for (const auto& [row, yieldVar] : vYield) {
      model.AddLinearConstraint(yieldVar <= yieldVar.upper_bound() * vSetup(row));
    }

    const auto holding_term            = [&](cItem i, cPeriod t) { return i->hold * vStock(i, t); };
    const auto setup_term              = [&](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
    const LinearExpression holdingCost = sumExpr(sSTOCK, holding_term).build();
    const LinearExpression setupCost   = sumExpr(sPRODUCE, setup_term).build();
    model.Minimize(holdingCost + setupCost);
  }
};

}  // namespace planning::mathopt
