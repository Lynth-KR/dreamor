// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/order.hpp - the hand-written arms of the order family, which ../order.hpp describes.
#pragma once

#include "../order.hpp"
#include "arm.hpp"
#include "lookup.hpp"

namespace order {

template <typename Index>
static void runHand(const Data& d, Result& r) {
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  Packer px;
  px.k = k;
  for (std::size_t j = 0; j < k; ++j) {
    px.size[j] = d.ents[j].size();
  }
  if constexpr (std::is_same_v<Index, lookup::ArrayIndex>) {
    if (px.span() > lookup::kArraySlotsPerRow * static_cast<long double>(d.rows)) {
      r.skip("the ids' product exceeds " + std::to_string(static_cast<int>(lookup::kArraySlotsPerRow)) +
             " slots a row");
      env.end();
      return;
    }
  }
  if (px.span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    r.skip("the ids do not pack into 64 bits");
    env.end();
    return;
  }
  // The position of each period by its id, and the id at each position, as a hand-coder keeps them beside the
  // entities.
  const std::vector<const Ent*> periods = periodsOf(d);
  std::vector<std::uint32_t> position(d.ents[last].size()), id_at(periods.size());
  for (std::size_t i = 0; i < periods.size(); ++i) {
    position[periods[i]->id] = static_cast<std::uint32_t>(i);
    id_at[i]                 = periods[i]->id;
  }

  std::vector<std::uint32_t> ids;  // The ids of the rows, k for each row.
  std::vector<IloNumVar> x;
  Index ix;
  if constexpr (std::is_same_v<Index, lookup::ArrayIndex>) { ix.span = static_cast<std::size_t>(px.span()); }
  r.measure("build", [&] {
    ids.reserve(d.rows * k);
    x.reserve(d.rows);
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      for (std::size_t j = 0; j < k; ++j) {
        ids.push_back(row[j]->id);
      }
      x.emplace_back(env, 0, tagOf(serialsOf(row, k), k, 0), ILOFLOAT);
    });
  });
  r.measure("index", [&] {
    ix.reserve(d.rows);
    for (std::uint32_t i = 0; i < d.rows; ++i) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t j = 0; j < k; ++j) {
        id[j] = ids[i * k + j];
      }
      ix.add(px(id), i);
    }
  });
  const auto rowIds = [&](std::size_t i) {
    std::array<std::uint32_t, kMaxColumns> id{};
    for (std::size_t j = 0; j < k; ++j) {
      id[j] = ids[i * k + j];
    }
    return id;
  };
  // Returns the tag of x at the ids in id, with the period moved back positions earlier, or 0 if no row has them.
  const auto at = [&](std::array<std::uint32_t, kMaxColumns> id, std::uint32_t back) -> std::uint64_t {
    id[last]              = id_at[position[id[last]] - back];
    const std::uint32_t e = ix.find(px(id));
    return e == Index::kNone ? 0 : ubOf(x[e]);
  };
  r.measure("step", [&] {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < d.rows; ++i) {
      const std::uint32_t p = position[ids[i * k + last]];
      if (p > 0) { sum += periods[p - 1]->serial + 1; }
    }
    r.check("step", sum);
  });
  r.measure("lag", [&] {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < d.rows; ++i) {
      const auto id = rowIds(i);
      if (position[id[last]] >= kLag) { sum += at(id, kLag); }
    }
    r.check("lag", sum);
  });
  r.measure("window", [&] {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < d.rows; ++i) {
      const auto id = rowIds(i);
      for (std::uint32_t l = 0; l < kWindow && l <= position[id[last]]; ++l) {
        sum += at(id, l);
      }
    }
    r.check("window", sum);
  });
  env.end();
}

// The map arm, the usual code: one std::map from a row's entities to its variable.
static void runMap(const Data& d, Result& r) {
  using Key           = std::array<const Ent*, kMaxColumns>;  // The columns past k stay null.
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  const std::vector<const Ent*> periods = periodsOf(d);  // The serial of a period is its position in periods.
  std::map<Key, IloNumVar> x;
  std::vector<Key> rows;
  r.measure("build", [&] {
    rows.reserve(d.rows);
    d.forEachRow([&](const Key& row) {
      Key key{};
      std::copy_n(row.begin(), k, key.begin());
      rows.push_back(key);
      x.emplace(key, IloNumVar(env, 0, tagOf(serialsOf(key, k), k, 0), ILOFLOAT));
    });
  });
  r.measure("index", [] {});
  const auto at = [&](Key key, std::uint32_t back) -> std::uint64_t {
    key[last]     = periods[key[last]->serial - back];
    const auto it = x.find(key);
    return it == x.end() ? 0 : ubOf(it->second);
  };
  r.measure("step", [&] {
    std::uint64_t sum = 0;
    for (const Key& key : rows) {
      const std::uint32_t p = key[last]->serial;
      if (p > 0) { sum += periods[p - 1]->serial + 1; }
    }
    r.check("step", sum);
  });
  r.measure("lag", [&] {
    std::uint64_t sum = 0;
    for (const Key& key : rows) {
      if (key[last]->serial >= kLag) { sum += at(key, kLag); }
    }
    r.check("lag", sum);
  });
  r.measure("window", [&] {
    std::uint64_t sum = 0;
    for (const Key& key : rows) {
      for (std::uint32_t l = 0; l < kWindow && l <= key[last]->serial; ++l) {
        sum += at(key, l);
      }
    }
    r.check("window", sum);
  });
  env.end();
}

static void run(const std::string& impl, const Data& d, const Samples&, Result& r) {
  if (d.k == 1) {
    r.skip("no k = 1: the family's operations need a period beside k1");
    return;
  }
  if (impl == "identity") {
    runHand<lookup::MapIndex<std::hash<std::uint64_t>>>(d, r);
  } else if (impl == "mixed") {
    runHand<lookup::MapIndex<MixedHash>>(d, r);
  } else if (impl == "array") {
    runHand<lookup::ArrayIndex>(d, r);
  } else if (impl == "map") {
    runMap(d, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace order
