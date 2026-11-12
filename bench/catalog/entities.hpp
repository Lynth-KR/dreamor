// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/entities.hpp - the data family: entities loaded and found by their keys, and the values that a solve hands
// back.
//
// The family has a design of its own, not the fraction of the rows, because entities have no columns to group or
// order. Its factors are N, the type of the key, and access. The key is an integer id, or a 13-character code such as
// ITEM000012345. access is the order of the lookups: the order in which the entities were loaded, or a random one.
// The phases are the same in every arm:
// - load: the N entities, one at a time, each checked against the ones before it, so that no key is held twice.
// - find_hit: each entity, found by its key.
// - find_miss: as many keys that no entity has.
// - build: a variable for each entity. The phase is not part of the total.
// - solution: the value of every variable, as a solver hands the values back after a solve.
// - value: the value of each entity.
//
// dreamor's EntityVector and SolutionTable are compared with two hand-written arms:
// - umap keeps a std::vector of the entities and a std::unordered_map from the key to the entity's place. It keeps the
//   variables and the values in arrays by that place.
// - map, the usual code, keeps std::maps by the key.
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

inline double tagOf(std::uint32_t serial) {
  return ::tagOf(std::array<std::uint32_t, 1>{serial}, 1, 2);
}

// A stand-in for a solver. It hands back the upper bound of each variable, which is the variable's tag, as the
// variable's value.
struct Solver {
  IloEnv env;

  [[nodiscard]] IloEnv getEnv() const { return env; }

  void getValues(IloNumArray values, const IloNumVarArray vars) const {
    for (IloInt i = 0; i < vars.getSize(); ++i) {
      values.add(vars[i].getUB());
    }
  }

  [[nodiscard]] IloNum getValue(const IloNumVar& var) const { return var.getUB(); }
};

// The keys: N keys of entities, in load order, and N keys that no entity has.
template <typename K>
struct Keys {
  std::vector<K> present, absent;
  std::vector<std::uint32_t> order;  // The places of the entities, in the order in which the lookups take them.
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
  if (random_order) { std::shuffle(keys.order.begin(), keys.order.end(), g); }
  return keys;
}

}  // namespace entities
