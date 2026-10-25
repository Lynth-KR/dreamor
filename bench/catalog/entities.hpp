// SPDX-License-Identifier: Apache-2.0
// catalog/entities.hpp - the data family: entities loaded and found by their key, and the
// values a solve hands back
//
// Its own design, not the rows' fraction: entities have no columns to group or order, so the
// factors are the key's type (an integer id or a 13-character code such as ITEM000012345), N, and
// access, the order the lookups come in (the order the entities were loaded in, or a random one).
// Phases, the same in every arm:
//   load       the N entities, one at a time, each checked against the ones before (no key twice)
//   find_hit   each entity by its key
//   find_miss  as many keys no entity has
//   build      a variable an entity (not part of the total)
//   solution   every variable's value, as a solver hands them back after a solve
//   value      each entity's value
//
// dreamor's EntityVector and ValueTable are set against a std::vector with a std::unordered_map
// from the key to the entity's place (`umap`), whose variables and values are arrays by that
// place; and the usual code, std::maps by the key (`map`).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "shared.hpp"

namespace entities {

using catalog::ubOf;

template <typename K>
struct Item {
  K code;
  std::uint32_t serial;
};
template <typename K>
const K& primaryKey(const Item<K>& item) {
  return item.code;
}
inline constexpr dreamor::ColumnName kItemName{"item"};
template <typename K>
using ItemCol = dreamor::Col<const Item<K>*, kItemName>;

inline double tagOf(std::uint32_t serial) {
  return ::tagOf(std::array<std::uint32_t, 1>{serial}, 1, 2);
}

// What a solver hands back: every variable's value, here its upper bound (its tag).
struct Solver {
  IloEnv env;
  [[nodiscard]] IloEnv getEnv() const { return env; }
  void getValues(IloNumArray values, const IloNumVarArray vars) const {
    for (IloInt i = 0; i < vars.getSize(); ++i) values.add(vars[i].getUB());
  }
  [[nodiscard]] IloNum getValue(const IloNumVar& var) const { return var.getUB(); }
};

// The keys: N that entities have, in load order, and N that none has.
template <typename K>
struct Keys {
  std::vector<K> present, absent;
  std::vector<std::uint32_t> order;  // the entities' places, in the order the lookups take them
};

template <typename K>
Keys<K> makeKeys(std::size_t n, bool random_order, std::uint64_t seed) {
  std::mt19937_64 g(seed * 2654435761ULL + 5);
  std::vector<std::uint32_t> numbers(2 * n);
  std::iota(numbers.begin(), numbers.end(), 0U);
  std::shuffle(numbers.begin(), numbers.end(), g);
  const auto keyOf = [](std::uint32_t number) {
    if constexpr (std::is_same_v<K, std::string>) {
      char text[16];
      std::snprintf(text, sizeof text, "ITEM%09u", number);
      return std::string(text);
    } else {
      return number;
    }
  };
  Keys<K> keys;
  keys.present.reserve(n);
  keys.absent.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    keys.present.push_back(keyOf(numbers[i]));
    keys.absent.push_back(keyOf(numbers[n + i]));
  }
  keys.order.resize(n);
  std::iota(keys.order.begin(), keys.order.end(), 0U);
  if (random_order) std::shuffle(keys.order.begin(), keys.order.end(), g);
  return keys;
}

// --- dreamor ---------------------------------------------------------------------------------
template <typename K>
static void runDreamor(const Keys<K>& keys, Result& r) {
  using Col = ItemCol<K>;
  struct Tag {
    double operator()(Col item) const { return tagOf(item->serial); }
  };
  IloEnv env;
  const Solver solver{env};
  dreamor::EntityVector<Item<K>> items;
  r.measure("load", [&] {
    for (std::size_t i = 0; i < keys.present.size(); ++i) items.emplace(keys.present[i], static_cast<std::uint32_t>(i));
  });
  r.measure("find_hit", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) sum += items.find(keys.present[i])->serial + 1;
    r.check("find_hit", sum);
  });
  r.measure("find_miss", [&] {
    std::uint64_t found = 0;
    for (const std::uint32_t i : keys.order) found += items.find(keys.absent[i]) != items.end();
    r.check("find_miss", found);
  });
  std::vector<const Item<K>*> by_place;  // the entities in load order, for the lookups below
  for (const Item<K>& item : items) by_place.push_back(&item);
  dreamor::SetOf<Col> sITEMS;
  std::optional<dreamor::concert::VarTable<Col>> vX;
  r.measure("build", [&] {
    sITEMS = dreamor::makeSet<Col>(items).collect();
    vX.emplace(env, sITEMS, 0, Tag{}, ILOFLOAT);
  });
  using Values = decltype(vX->solution(solver));
  std::optional<Values> plan;
  r.measure("solution", [&] { plan.emplace(vX->solution(solver)); });
  {
    std::uint64_t sum = 0;
    for (const auto& [row, value] : *plan) sum += static_cast<std::uint64_t>(value);
    r.check("solution", sum);
  }
  r.measure("value", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) sum += static_cast<std::uint64_t>((*plan)(Col{by_place[i]}));
    r.check("value", sum);
  });
  env.end();
}

// --- hand-written ----------------------------------------------------------------------------
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
    for (const std::uint32_t i : keys.order) sum += items[place.find(keys.present[i])->second].serial + 1;
    r.check("find_hit", sum);
  });
  r.measure("find_miss", [&] {
    std::uint64_t found = 0;
    for (const std::uint32_t i : keys.order) found += place.find(keys.absent[i]) != place.end();
    r.check("find_miss", found);
  });
  IloNumVarArray x(env);
  r.measure("build", [&] {
    for (const Item<K>& item : items) x.add(IloNumVar(env, 0, tagOf(item.serial), ILOFLOAT));
  });
  IloNumArray values(env);
  r.measure("solution", [&] { solver.getValues(values, x); });
  {
    std::uint64_t sum = 0;
    for (IloInt i = 0; i < values.getSize(); ++i) sum += static_cast<std::uint64_t>(values[i]);
    r.check("solution", sum);
  }
  r.measure("value", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) sum += static_cast<std::uint64_t>(values[static_cast<IloInt>(i)]);
    r.check("value", sum);
  });
  env.end();
}

// --- the usual hand-written code --------------------------------------------------------------
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
    for (const std::uint32_t i : keys.order) sum += items.find(keys.present[i])->second.serial + 1;
    r.check("find_hit", sum);
  });
  r.measure("find_miss", [&] {
    std::uint64_t found = 0;
    for (const std::uint32_t i : keys.order) found += items.contains(keys.absent[i]);
    r.check("find_miss", found);
  });
  std::map<K, IloNumVar> x;
  r.measure("build", [&] {
    for (const auto& [code, item] : items) x.emplace(code, IloNumVar(env, 0, tagOf(item.serial), ILOFLOAT));
  });
  std::map<K, double> values;
  r.measure("solution", [&] {
    for (const auto& [code, var] : x) values.emplace(code, solver.getValue(var));
  });
  {
    std::uint64_t sum = 0;
    for (const auto& [code, value] : values) sum += static_cast<std::uint64_t>(value);
    r.check("solution", sum);
  }
  r.measure("value", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) sum += static_cast<std::uint64_t>(values.at(keys.present[i]));
    r.check("value", sum);
  });
  env.end();
}

template <typename K>
static void runKey(const std::string& impl, std::size_t n, bool random_order, std::uint64_t seed, Result& r) {
  Keys<K> keys;
  r.setup("keys", [&] { keys = makeKeys<K>(n, random_order, seed); });
  if (impl == "dreamor") {
    runDreamor(keys, r);
  } else if (impl == "umap") {
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
