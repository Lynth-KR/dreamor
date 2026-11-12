// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/entities.hpp - the hand-written arms of the data family, which ../entities.hpp describes.
#pragma once

#include "../entities.hpp"
#include "arm.hpp"

namespace entities {

// The umap arm: a std::vector of the entities, and a std::unordered_map from the key to the entity's place.
template <typename K>
static void runUmap(const Keys<K>& keys, Result& r) {
  IloEnv env;
  const Solver solver{env};
  std::vector<Item<K>> items;
  std::unordered_map<K, std::uint32_t> place;
  r.measure("load", [&] {
    for (std::size_t i = 0; i < keys.present.size(); ++i) {
      if (place.try_emplace(keys.present[i], static_cast<std::uint32_t>(items.size())).second) {
        items.push_back({keys.present[i], static_cast<std::uint32_t>(i)});
      }
    }
  });
  r.measure("find_hit", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) {
      sum += items[place.find(keys.present[i])->second].serial + 1;
    }
    r.check("find_hit", sum);
  });
  r.measure("find_miss", [&] {
    std::uint64_t found = 0;
    for (const std::uint32_t i : keys.order) {
      found += place.find(keys.absent[i]) != place.end();
    }
    r.check("find_miss", found);
  });
  IloNumVarArray x(env);
  r.measure("build", [&] {
    for (const Item<K>& item : items) {
      x.add(IloNumVar(env, 0, tagOf(item.serial), ILOFLOAT));
    }
  });
  IloNumArray values(env);
  r.measure("solution", [&] { solver.getValues(values, x); });
  {
    std::uint64_t sum = 0;
    for (IloInt i = 0; i < values.getSize(); ++i) {
      sum += static_cast<std::uint64_t>(values[i]);
    }
    r.check("solution", sum);
  }
  r.measure("value", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) {
      sum += static_cast<std::uint64_t>(values[static_cast<IloInt>(i)]);
    }
    r.check("value", sum);
  });
  env.end();
}

// The map arm, the usual code: std::maps by the key.
template <typename K>
static void runMap(const Keys<K>& keys, Result& r) {
  IloEnv env;
  const Solver solver{env};
  std::map<K, Item<K>> items;
  r.measure("load", [&] {
    for (std::size_t i = 0; i < keys.present.size(); ++i) {
      items.try_emplace(keys.present[i], Item<K>{keys.present[i], static_cast<std::uint32_t>(i)});
    }
  });
  r.measure("find_hit", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) {
      sum += items.find(keys.present[i])->second.serial + 1;
    }
    r.check("find_hit", sum);
  });
  r.measure("find_miss", [&] {
    std::uint64_t found = 0;
    for (const std::uint32_t i : keys.order) {
      found += items.contains(keys.absent[i]);
    }
    r.check("find_miss", found);
  });
  std::map<K, IloNumVar> x;
  r.measure("build", [&] {
    for (const auto& [code, item] : items) {
      x.emplace(code, IloNumVar(env, 0, tagOf(item.serial), ILOFLOAT));
    }
  });
  std::map<K, double> values;
  r.measure("solution", [&] {
    for (const auto& [code, var] : x) {
      values.emplace(code, solver.getValue(var));
    }
  });
  {
    std::uint64_t sum = 0;
    for (const auto& [code, value] : values) {
      sum += static_cast<std::uint64_t>(value);
    }
    r.check("solution", sum);
  }
  r.measure("value", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) {
      sum += static_cast<std::uint64_t>(values.at(keys.present[i]));
    }
    r.check("value", sum);
  });
  env.end();
}

template <typename K>
static void runKey(const std::string& impl, std::size_t n, bool random_order, std::uint64_t seed, Result& r) {
  Keys<K> keys;
  r.setup("keys", [&] { keys = makeKeys<K>(n, random_order, seed); });
  if (impl == "umap") {
    runUmap(keys, r);
  } else if (impl == "map") {
    runMap(keys, r);
  } else {
    r.skip("no arm " + impl);
  }
}

static void run(const std::string& impl,
                const std::string& key,
                const std::string& access,
                std::size_t n,
                std::uint64_t seed,
                Result& r) {
  if (key == "string") {
    runKey<std::string>(impl, n, access == "random", seed, r);
  } else {
    runKey<std::uint32_t>(impl, n, access == "random", seed, r);
  }
}

}  // namespace entities
