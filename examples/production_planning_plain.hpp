// SPDX-License-Identifier: MIT-0
//
// The same lot-sizing model as production_planning_concert.hpp, written with plain Concert and
// std::map - the way it is usually done without index sets, on the data alone and with no part of
// the library: items, machines and routes by their row numbers. Kept for comparison (and the tests
// check both build the same model).
#pragma once

#include <format>
#include <map>
#include <tuple>
#include <utility>

#include <ilcplex/ilocplex.h>

#include "production_planning_data.hpp"

namespace planning {

struct PlainModel {
  // (machine, item, period) and (item, period), by row number.
  std::map<std::tuple<int, int, int>, IloNumVar> yieldVars, setupVars;
  std::map<std::pair<int, int>, IloNumVar> stockVars;

  PlainModel(IloEnv env, const Instance& in) {
    for (const RouteRecord& route : in.routes) {
      const MachineRecord& machine = in.machines[route.machine];
      const ItemRecord& item       = in.items[route.item];
      for (int period : in.periods) {
        yieldVars[{route.machine, route.item, period}] =
            IloNumVar(env,
                      0,
                      machine.capacity / item.time,
                      ILOFLOAT,
                      std::format("Yield_{}_{}_{}", machine.name, item.name, period).c_str());
        setupVars[{route.machine, route.item, period}] =
            IloNumVar(env, 0, 1, ILOBOOL, std::format("Setup_{}_{}_{}", machine.name, item.name, period).c_str());
      }
    }
    for (int item = 0; item < std::ssize(in.items); ++item) {
      for (int period : in.periods) {
        stockVars[{item, period}] =
            IloNumVar(env, 0, IloInfinity, ILOFLOAT, std::format("Stock_{}_{}", in.items[item].name, period).c_str());
      }
    }
  }

  void build(IloEnv env, const IloModel& model, const Instance& in) {
    for (int item = 0; item < std::ssize(in.items); ++item) {
      for (const int period : in.periods) {
        IloExpr totalInFlow(env);
        for (const RouteRecord& route : in.routes) {
          if (route.item == item) { totalInFlow += yieldVars[{route.machine, item, period}]; }
        }
        if (period > 0) { totalInFlow += stockVars[{item, period - 1}]; }
        model.add(totalInFlow - in.items[item].demand[period] == stockVars[{item, period}]);
        totalInFlow.end();
      }
    }
    for (int machine = 0; machine < std::ssize(in.machines); ++machine) {
      bool routed = false;
      for (const RouteRecord& route : in.routes) {
        routed = routed || route.machine == machine;
      }
      if (!routed) { continue; }
      for (const int period : in.periods) {
        IloExpr totalLoad(env);
        for (const RouteRecord& route : in.routes) {
          if (route.machine == machine) {
            totalLoad += in.items[route.item].time * yieldVars[{machine, route.item, period}];
          }
        }
        model.add(totalLoad <= in.machines[machine].capacity);
        totalLoad.end();
      }
    }
    for (auto& [key, yieldVar] : yieldVars) {
      model.add(yieldVar <= yieldVar.getUB() * setupVars[key]);
    }

    IloExpr totalCost(env);
    for (auto& [key, stockVar] : stockVars) {
      totalCost += in.items[key.first].hold * stockVar;
    }
    for (auto& [key, setupVar] : setupVars) {
      totalCost += in.machines[std::get<0>(key)].setup * setupVar;
    }
    model.add(IloMinimize(env, totalCost));
    totalCost.end();
  }
};

}  // namespace planning
