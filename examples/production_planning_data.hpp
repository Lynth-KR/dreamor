// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The lot-sizing example's data (production_planning_sets.hpp states the model): items, machines
// and the instance generator, with no part of the library - the plain model and the benchmark's
// hand-written arms build on it alone.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

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
    for (std::size_t t = 0; t < periodCount; ++t) {
      in.periods.push_back(t);
    }
    for (std::size_t k = 0; k < itemCount; ++k) {
      auto item  = std::make_unique<Item>();
      item->name = "I" + std::to_string(k);
      item->time = 1.0 + (2.0 * u(g));
      item->hold = 0.5 + u(g);
      for (std::size_t t = 0; t < periodCount; ++t) {
        item->demand.push_back(u(g) < 0.3 ? 0.0 : std::round(10 + (30 * u(g))));
      }
      in.items.push_back(std::move(item));
    }
    for (std::size_t k = 0; k < machineCount; ++k) {
      auto machine      = std::make_unique<Machine>();
      machine->name     = "M" + std::to_string(k);
      machine->capacity = (60.0 * static_cast<double>(itemCount) / static_cast<double>(machineCount)) + (40.0 * u(g));
      machine->setup    = 20 + (30 * u(g));
      for (const auto& item : in.items) {
        if (u(g) < 0.6) { machine->items.push_back(item.get()); }
      }
      in.machines.push_back(std::move(machine));
    }
    for (const auto& item : in.items) {  // every item gets at least one machine
      bool covered = false;
      for (const auto& m : in.machines) {
        for (const Item* i : m->items) {
          covered = covered || i == item.get();
        }
      }
      if (!covered) { in.machines[g() % in.machines.size()]->items.push_back(item.get()); }
    }
    return in;
  }
};
}  // namespace planning
