// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/slice.hpp - the slice family: groups reached from the keys of another set, and one sum for each key.
//
// The family has two shapes, as group has. inner keeps every column but the period, so its groups are runs. lead
// keeps every column but k1. Each shape has a driver: the set that a model walks to write one expression for each
// key, such as a balance over ITEMS x PERIODS. The driver holds every key of the shape. Beside each key it holds one
// key that no row has, whose last column is swapped for an entity of its own. The two keys of a pair come in a random
// order. So half the keys reach an empty slice, as some (item, period) pairs have no production.
// The phases are the same in every arm. build makes the rows and x on them. Then, for each shape, prep_<shape> makes
// the grouping and what a key reaches its group through. slice_<shape> makes one expression for each key of the
// driver: x over the rows of the key's group. On the inner shape, split makes two expressions for each key in one
// pass, over the rows in even periods and over the rows in odd periods. coef makes one expression for each key, with
// c(t) * x for each row.
//
// dreamor's groupBy, slice and sumExpr are compared with the hand-written groupings of group, each with what a key
// goes through:
// - identity and mixed find the number of the group in a std::unordered_map from the packed key.
// - sort sorts the packed keys and searches them with std::lower_bound.
// - map, the usual code, keeps a std::map from the entities of the key to the rows.
// identity, mixed and sort take the groups of the inner shape as runs.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "group.hpp"
#include "shared.hpp"

namespace slice {

using catalog::consume;
using catalog::keyOf;
using catalog::MixedHash;
using catalog::Packer;
template <typename Arm>
using KeyOf = std::array<typename Arm::Handle, kMaxColumns>;  // The columns of a shape's key come first.

inline double coefOf(std::uint32_t period_serial) {
  return 1.0 + static_cast<double>(period_serial % 7);
}

// The driver of a shape: the positions of its columns, its keys in the order in which a model walks them, and the
// shadows. A shadow is an entity of the key's last column that no row holds. The driver has one shadow for each entity
// of that column.
template <typename Arm>
struct DriverOf {
  std::string shape;
  std::vector<std::size_t> kept;
  std::vector<KeyOf<Arm>> keys;
  typename Arm::Ents shadows;
  std::vector<typename Arm::Handle> shadow_handles;  // By the serial of the entity that each shadow stands in for.
};

template <typename Arm>
using DriversOf = std::array<DriverOf<Arm>, 2>;  // The driver of inner, then the driver of lead.

template <typename Handle>
inline std::uint64_t keyCheck(const std::array<Handle, kMaxColumns>& key, std::size_t m) {
  std::array<std::uint32_t, kMaxColumns> s{};
  for (std::size_t i = 0; i < m; ++i) {
    s[i] = key[i]->serial;
  }
  return keyOf(s, m);
}

template <typename Arm>
static DriverOf<Arm> makeDriver(const DataOf<Arm>& d, const std::string& shape, std::uint64_t seed) {
  using Handle = typename Arm::Handle;
  using Key    = KeyOf<Arm>;
  DriverOf<Arm> dr;
  dr.shape                = shape;
  dr.kept                 = group::keptBy(shape, d.k);
  const std::size_t m     = dr.kept.size();
  const std::size_t count = d.ents[dr.kept.back()].size();
  const bool inner        = shape == "inner";
  dr.shadows.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto s = static_cast<std::uint32_t>(count + i);
    Arm::add(dr.shadows, Ent{s, s});
  }
  Arm::freeze(dr.shadows);
  dr.shadow_handles.reserve(count);
  for (const Ent& e : dr.shadows) {
    dr.shadow_handles.push_back(Arm::handle(dr.shadows, e));
  }
  // The distinct keys of the lead shape, found through an open-addressing table of their places in distinct.
  std::vector<Key> distinct;
  distinct.reserve(inner ? 0 : d.rows);
  dr.keys.reserve(2 * (inner ? d.rows / 2 : d.rows));
  std::size_t capacity = 16;
  while (capacity < 2 * d.rows) {
    capacity *= 2;
  }
  std::vector<std::uint32_t> table(inner ? 0 : capacity, std::numeric_limits<std::uint32_t>::max());
  const auto fresh = [&](const Key& key) {
    std::uint64_t h = 0;
    for (std::size_t i = 0; i < m; ++i) {
      h = (h ^ key[i]->serial) * 0x9e3779b97f4a7c15ULL;
    }
    for (std::size_t slot = mix(h) & (capacity - 1);; slot = (slot + 1) & (capacity - 1)) {
      if (table[slot] == std::numeric_limits<std::uint32_t>::max()) {
        table[slot] = static_cast<std::uint32_t>(distinct.size());
        distinct.push_back(key);
        return true;
      }
      if (distinct[table[slot]] == key) { return false; }
    }
  };
  std::mt19937_64 g(seed * 15485863 + 11);
  Key previous{};
  d.forEachRow([&](const std::array<Handle, kMaxColumns>& row) {
    Key key{};
    for (std::size_t i = 0; i < m; ++i) {
      key[i] = row[dr.kept[i]];
    }
    // The keys of the inner shape come as runs, so a key of the inner shape is a repeat if it is the previous key.
    if (inner ? key == previous : !fresh(key)) { return; }
    previous      = key;
    Key absent    = key;
    absent[m - 1] = dr.shadow_handles[key[m - 1]->serial];
    if ((g() & 1) != 0) { std::swap(key, absent); }
    dr.keys.push_back(key);
    dr.keys.push_back(absent);
  });
  return dr;
}

}  // namespace slice
