// SPDX-License-Identifier: Apache-2.0
// Multi-machine, multi-period lot sizing - the example model of the library. This file holds
// what does not depend on a solver: the data, the columns, the instance generator and the
// sets. production_planning.hpp (Concert) and production_planning_mathopt.hpp (MathOpt)
// each build the same model on these sets.
//
//   sets    ROUTES  = {m in MACHINES, i in ITEMS[m]}            which machine can make what
//           PRODUCE = {(m,i) in ROUTES, t in PERIODS}
//           STOCK   = {i in ITEMS, t in PERIODS}
//   vars    Yield[m,i,t] >= 0, <= cap[m] / time[i]              quantity made
//           Setup[m,i,t] binary                                  setup
//           Stock[i,t]   >= 0                                    stock at the end of t
//   min     sum {(i,t) in STOCK} hold[i] * Stock[i,t] + sum {(m,i,t) in PRODUCE} setup[m] * Setup[m,i,t]
//   s.t.    balance  {(i,t) in STOCK}:   Stock[i,prev(t)] + sum {m: (m,i,t) in PRODUCE} Yield[m,i,t] - demand[i,t] =
//   Stock[i,t]
//           capacity {(m,t)}:            sum {i: (m,i,t) in PRODUCE} time[i] * Yield[m,i,t] <= cap[m]
//           setup    {(m,i,t) in PRODUCE}: Yield[m,i,t] <= ub(Yield[m,i,t]) * Setup[m,i,t]
//
//   Naming: sets sSCREAMING_SNAKE, variable families vPascalCase (the AMPL name with a prefix);
//   callables that take columns snake_case, everything else camelCase.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "dreamor/index_set.hpp"

namespace planning {
struct Item {
  std::string name;
  double time{};               // machine time per unit
  double hold{};               // holding cost per unit and period
  std::vector<double> demand;  // per period
};

struct Machine {
  std::string name;
  double capacity{};               // time per period
  double setup{};                  // cost of a setup
  std::vector<const Item*> items;  // what it can make
};

// Name tokens for variable names: Yield_M1_I3_0 ...
inline const std::string& keyName(const Item* i) {
  return i->name;
}
inline const std::string& keyName(const Machine* m) {
  return m->name;
}

using MachineCol = dreamor::Col<const Machine*, "machine">;
using ItemCol    = dreamor::Col<const Item*, "item">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;

struct Instance {
  std::vector<std::unique_ptr<Item>> items;
  std::vector<std::unique_ptr<Machine>> machines;
  std::vector<std::size_t> periods;

  // Deterministic random instance: every item runs on a few machines, demand fluctuates.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters): three counts, each named by its caller
  static Instance make(std::size_t machineCount,
                       std::size_t itemCount,
                       std::size_t periodCount,
                       std::uint32_t seed = 7) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    Instance in;
    std::mt19937 g(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (std::size_t t = 0; t < periodCount; ++t) in.periods.push_back(t);
    for (std::size_t k = 0; k < itemCount; ++k) {
      auto item  = std::make_unique<Item>();
      item->name = "I" + std::to_string(k);
      item->time = 1.0 + (2.0 * u(g));
      item->hold = 0.5 + u(g);
      for (std::size_t t = 0; t < periodCount; ++t)
        item->demand.push_back(u(g) < 0.3 ? 0.0 : std::round(10 + (30 * u(g))));
      in.items.push_back(std::move(item));
    }
    for (std::size_t k = 0; k < machineCount; ++k) {
      auto machine      = std::make_unique<Machine>();
      machine->name     = "M" + std::to_string(k);
      machine->capacity = (60.0 * static_cast<double>(itemCount) / static_cast<double>(machineCount)) + (40.0 * u(g));
      machine->setup    = 20 + (30 * u(g));
      for (const auto& item : in.items)
        if (u(g) < 0.6) machine->items.push_back(item.get());
      in.machines.push_back(std::move(machine));
    }
    for (const auto& item : in.items) {  // every item gets at least one machine
      bool covered = false;
      for (const auto& m : in.machines)
        for (const Item* i : m->items) covered = covered || i == item.get();
      if (!covered) in.machines[g() % in.machines.size()]->items.push_back(item.get());
    }
    return in;
  }
};

// Sets first (a base class is initialized before the members that use it), then variables.
struct Sets {
  dreamor::SetOf<PeriodCol> sPERIODS;
  dreamor::SetOf<MachineCol, ItemCol> sROUTES;
  dreamor::SetOf<MachineCol, ItemCol, PeriodCol> sPRODUCE;
  dreamor::SetOf<ItemCol, PeriodCol> sSTOCK;

  explicit Sets(const Instance& in) {
    const auto items_of = [](MachineCol m) { return m->items; };

    sPERIODS = dreamor::makeSet<PeriodCol>(in.periods).collect();
    sROUTES  = dreamor::makeSet<MachineCol, ItemCol>(in.machines, items_of).collect();
    sPRODUCE = dreamor::makeSet<PeriodCol>(sROUTES, in.periods).collect();
    sSTOCK   = dreamor::makeSet<ItemCol, PeriodCol>(in.items, in.periods).collect();
  }
};
}  // namespace planning
