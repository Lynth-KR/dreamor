// SPDX-License-Identifier: Apache-2.0
// The same lot-sizing model as production_planning.hpp, written with plain Concert and
// std::map - the way it is usually done without index sets. Kept for comparison (and the
// tests check both build the same model).
#pragma once

#include <cstddef>
#include <format>
#include <map>
#include <tuple>
#include <utility>

#include "production_planning.hpp"

namespace planning {

struct PlainModel {
  std::map<std::tuple<const Machine*, const Item*, std::size_t>, IloNumVar> yieldVars, setupVars;
  std::map<std::pair<const Item*, std::size_t>, IloNumVar> stockVars;

  PlainModel(IloEnv env, const Instance& in) {
    for (const auto& machine : in.machines)
      for (const Item* item : machine->items)
        for (std::size_t period : in.periods) {
          yieldVars[{machine.get(), item, period}] =
              IloNumVar(env,
                        0,
                        machine->capacity / item->time,
                        ILOFLOAT,
                        std::format("Yield_{}_{}_{}", machine->name, item->name, period).c_str());
          setupVars[{machine.get(), item, period}] =
              IloNumVar(env, 0, 1, ILOBOOL, std::format("Setup_{}_{}_{}", machine->name, item->name, period).c_str());
        }
    for (const auto& item : in.items)
      for (std::size_t period : in.periods)
        stockVars[{item.get(), period}] =
            IloNumVar(env, 0, IloInfinity, ILOFLOAT, std::format("Stock_{}_{}", item->name, period).c_str());
  }

  void build(IloEnv env, IloModel model, const Instance& in) {
    for (const auto& item : in.items)
      for (std::size_t period : in.periods) {
        IloExpr totalInFlow(env);
        for (const auto& machine : in.machines)
          for (const Item* routedItem : machine->items)
            if (routedItem == item.get()) totalInFlow += yieldVars[{machine.get(), item.get(), period}];
        if (period > 0) totalInFlow += stockVars[{item.get(), period - 1}];
        model.add(totalInFlow - item->demand[period] == stockVars[{item.get(), period}]);
        totalInFlow.end();
      }
    for (const auto& machine : in.machines) {
      if (machine->items.empty()) continue;
      for (std::size_t period : in.periods) {
        IloExpr totalLoad(env);
        for (const Item* item : machine->items) totalLoad += item->time * yieldVars[{machine.get(), item, period}];
        model.add(totalLoad <= machine->capacity);
        totalLoad.end();
      }
    }
    for (auto& [key, yieldVar] : yieldVars) model.add(yieldVar <= yieldVar.getUB() * setupVars[key]);

    IloExpr totalCost(env);
    for (auto& [key, stockVar] : stockVars) totalCost += key.first->hold * stockVar;
    for (auto& [key, setupVar] : setupVars) totalCost += std::get<0>(key)->setup * setupVar;
    model.add(IloMinimize(env, totalCost));
    totalCost.end();
  }
};

}  // namespace planning
