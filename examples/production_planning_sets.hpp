// SPDX-License-Identifier: MIT-0
//
// Multi-machine, multi-period lot sizing - the example model of the library. This file holds
// what does not depend on a solver: the entities, read from the rows of production_planning_data.hpp,
// and the sets. production_planning_concert.hpp (Concert), production_planning_mathopt.hpp (MathOpt) and
// production_planning_highs.hpp (HiGHS) each build the same model on these sets.
//
//   entities  Item, Machine, and Route - a machine that can make an item, a relation of its own: a machine
//             does not list its items, a route refers to its machine and its item
//   sets      ROUTES  = {(m, i) | (m, i) the machine and the item of a route}
//             PRODUCE = {(m, i, t) | (m, i) in ROUTES, t in PERIODS}
//             STOCK   = {(i, t) | i an item, t in PERIODS}
//   vars      Yield[m, i, t], from 0 to the most machine m can make of item i in a period - quantity made
//             Setup[m, i, t], 0 or 1 - set up for item i on machine m in period t
//             Stock[i, t], from 0 - stock of item i at the end of period t
//   minimize  the holding cost of every stock plus the setup cost of every setup
//   rows      balance, for each item and period: the stock carried in, plus what every route of the item makes,
//             less the demand, is the stock carried out
//             capacity, for each machine and period: the time its routes take is at most its capacity
//             setup, for each route and period: nothing is made without a setup
//
//   Naming: columns cPascalCase, sets sSCREAMING_SNAKE, variable families vPascalCase; callables that take columns
//   snake_case, everything else camelCase.
#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "dreamor/index_set.hpp"
#include "production_planning_data.hpp"

namespace planning {

using dreamor::index_set::set_builder::in;
using dreamor::index_set::set_builder::of;

struct Item {
  std::string name;
  double time{};               // machine time per unit
  double hold{};               // holding cost per unit and period
  std::vector<double> demand;  // per period

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Machine {
  std::string name;
  double capacity{};  // time per period
  double setup{};     // cost of a setup

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

// Which machine can make which item. The many refer to the one: a route knows its machine and its item.
struct Route {
  dreamor::Ref<Machine> machine;
  dreamor::Ref<Item> item;

  [[nodiscard]] auto primaryKey() const { return std::tuple{machine, item}; }
};

DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_ENTITY_COLUMN(cRoute, Route);
DREAMOR_VALUE_COLUMN(cPeriod, int);

// The rows read into the model's entities, each table frozen before the one that refers to it.
struct Entities {
  dreamor::EntityVector<Item> items;
  dreamor::EntityVector<Machine> machines;
  dreamor::EntityVector<Route> routes;

  explicit Entities(const Instance& instance) {
    items.reserve(instance.items.size());
    for (const ItemRecord& row : instance.items) {
      static_cast<void>(
          items.emplace(Item{.name = row.name, .time = row.time, .hold = row.hold, .demand = row.demand}));
    }
    items.freeze();
    machines.reserve(instance.machines.size());
    for (const MachineRecord& row : instance.machines) {
      static_cast<void>(machines.emplace(Machine{.name = row.name, .capacity = row.capacity, .setup = row.setup}));
    }
    machines.freeze();
    routes.reserve(instance.routes.size());
    for (const RouteRecord& row : instance.routes) {
      const Route route{
          .machine = machines.at(instance.machines[row.machine].name),
          .item    = items.at(instance.items[row.item].name),
      };
      static_cast<void>(routes.emplace(route));
    }
    routes.freeze();
  }
};

// Entities first, then the sets on them (a base class is initialized before the members that use it), then variables.
// Each set is built where it is declared, in declaration order, so a set reads the ones declared before it.
struct Sets : Entities {
  static constexpr auto ends_of = [](cRoute r) { return std::tuple<cMachine, cItem>{r->machine, r->item}; };

  const dreamor::IndexSet<cPeriod> sPERIODS;
  const dreamor::IndexSet<cMachine, cItem> sROUTES;
  const dreamor::IndexSet<cMachine, cItem, cPeriod> sPRODUCE;
  const dreamor::IndexSet<cItem, cPeriod> sSTOCK;

  explicit Sets(const Instance& instance)
      : Entities{instance},
        sPERIODS{of<cPeriod>(in<cPeriod>(instance.periods)).build()},
        sROUTES{of<cRoute>(in<cRoute>(routes)).build().select(ends_of)},
        sPRODUCE{of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(sROUTES), in<cPeriod>(instance.periods)).build()},
        sSTOCK{of<cItem, cPeriod>(in<cItem>(items), in<cPeriod>(instance.periods)).build()} {}
};

}  // namespace planning
