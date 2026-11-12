// SPDX-License-Identifier: MIT-0
//
// The lot-sizing example's data (production_planning_sets.hpp states the model), with no part of the
// library: the rows a model reads, as a file or a database would hand them over - items, machines, and
// routes, the machines that can make each item. The generator stands for that file. Each model reads
// the rows into its own containers as it goes: the dreamor models into EntityVectors, the plain model
// and the benchmark's hand-written arms into std::vectors.
#pragma once

#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace planning {

struct ItemRecord {
  std::string name;
  double time{};               // machine time per unit
  double hold{};               // holding cost per unit and period
  std::vector<double> demand;  // per period
};

struct MachineRecord {
  std::string name;
  double capacity{};  // time per period
  double setup{};     // cost of a setup
};

// A machine that can make an item, each named by its row in the file.
struct RouteRecord {
  int machine{};  // which of the machines
  int item{};     // which of the items
};

struct Instance {
  std::vector<ItemRecord> items;
  std::vector<MachineRecord> machines;
  std::vector<RouteRecord> routes;
  std::vector<int> periods;

  // Deterministic random instance: every item runs on a few machines, demand fluctuates.
  static Instance make(int machineCount, int itemCount, int periodCount, unsigned seed = 7) {
    Instance in;
    std::mt19937 g(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int t = 0; t < periodCount; ++t) {
      in.periods.push_back(t);
    }
    for (int k = 0; k < itemCount; ++k) {
      ItemRecord item{.name = "I" + std::to_string(k), .time = 1.0 + (2.0 * u(g)), .hold = 0.5 + u(g), .demand = {}};
      for (int t = 0; t < periodCount; ++t) {
        item.demand.push_back(u(g) < 0.3 ? 0.0 : std::round(10 + (30 * u(g))));
      }
      in.items.push_back(std::move(item));
    }
    for (int k = 0; k < machineCount; ++k) {
      const double capacity = (60.0 * itemCount / machineCount) + (40.0 * u(g));
      const double setup    = 20 + (30 * u(g));
      in.machines.push_back(MachineRecord{.name = "M" + std::to_string(k), .capacity = capacity, .setup = setup});
      for (int item = 0; item < itemCount; ++item) {
        if (u(g) < 0.6) { in.routes.push_back(RouteRecord{.machine = k, .item = item}); }
      }
    }
    for (int item = 0; item < itemCount; ++item) {  // every item gets at least one machine
      bool covered = false;
      for (const RouteRecord& route : in.routes) {
        covered = covered || route.item == item;
      }
      if (!covered) {
        in.routes.push_back(RouteRecord{.machine = static_cast<int>(g() % in.machines.size()), .item = item});
      }
    }
    return in;
  }
};

}  // namespace planning
