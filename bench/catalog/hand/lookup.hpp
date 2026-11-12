// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/lookup.hpp - the hand-written arms of the lookup family, which ../lookup.hpp describes.
#pragma once

#include "../lookup.hpp"
#include "arm.hpp"

namespace lookup {

// The index that a hand-written arm keeps from a packed key to an entry, through a std::unordered_map.
template <typename Hash>
struct MapIndex {
  std::unordered_map<std::uint64_t, std::uint32_t, Hash> map;

  void reserve(std::size_t n) { map.reserve(n); }

  void add(std::uint64_t key, std::uint32_t entry) { map.emplace(key, entry); }

  [[nodiscard]] std::uint32_t find(std::uint64_t key) const {
    const auto it = map.find(key);
    return it == map.end() ? kNone : it->second;
  }

  static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
};

// The index from a packed key to an entry, through a direct array over every packed key.
struct ArrayIndex {
  std::vector<std::uint32_t> slot;
  std::size_t span = 0;

  void reserve(std::size_t) { slot.assign(span, kNone); }

  void add(std::uint64_t key, std::uint32_t entry) { slot[key] = entry; }

  [[nodiscard]] std::uint32_t find(std::uint64_t key) const { return slot[key]; }

  static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
};

// A hand-coder writes a direct array only while it fits: at most this many slots for each row.
inline constexpr long double kArraySlotsPerRow = 16;

template <typename Index>
static void runHand(const Data& d, const Samples& s, Result& r) {
  const std::size_t k = d.k, last = d.last();
  const bool pairs = k > 1;  // Whether the cell has y. A cell at k = 1 has no period, so it has no y.
  IloEnv env;
  Packer px, py;
  px.k = k;
  for (std::size_t j = 0; j < k; ++j) {
    px.size[j] = d.ents[j].size();
  }
  py.k       = 2;
  py.size[0] = d.ents[0].size();
  py.size[1] = d.ents[last].size();
  if constexpr (std::is_same_v<Index, ArrayIndex>) {
    if (px.span() > kArraySlotsPerRow * static_cast<long double>(d.rows)) {
      r.skip("the ids' product exceeds " + std::to_string(static_cast<int>(kArraySlotsPerRow)) + " slots a row");
      env.end();
      return;
    }
  }
  if (px.span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    r.skip("the ids do not pack into 64 bits");
    env.end();
    return;
  }

  std::vector<std::uint32_t> ids;  // The ids of the rows, k for each row.
  std::vector<IloNumVar> x, y;
  std::vector<std::uint64_t> pair_keys;  // The keys of y: the packed (k1, period) pairs.
  Index ix, iy;
  if constexpr (std::is_same_v<Index, ArrayIndex>) {
    ix.span = static_cast<std::size_t>(px.span());
    iy.span = static_cast<std::size_t>(py.span());
  }
  const auto pairOf = [&](const std::uint32_t* row) {
    std::array<std::uint32_t, kMaxColumns> p{row[0], row[last]};
    return py(p);
  };
  r.measure("build", [&] {
    ids.reserve(d.rows * k);
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      for (std::size_t j = 0; j < k; ++j) {
        ids.push_back(row[j]->id);
      }
    });
    x.reserve(d.rows);
    // A tag needs the serials. A table for each column maps an id back to its serial.
    std::array<std::vector<std::uint32_t>, kMaxColumns> serial_of;
    for (std::size_t j = 0; j < k; ++j) {
      serial_of[j].resize(d.ents[j].size());
      for (const auto& e : d.ents[j]) {
        serial_of[j][e.id] = e.serial;
      }
    }
    // Each pair seen so far, to its place in pair_keys, which holds the pairs in the order of first appearance.
    std::unordered_map<std::uint64_t, std::uint32_t, MixedHash> seen;
    for (std::size_t i = 0; i < d.rows; ++i) {
      const std::uint32_t* row = &ids[i * k];
      std::array<std::uint32_t, kMaxColumns> serial{};
      for (std::size_t j = 0; j < k; ++j) {
        serial[j] = serial_of[j][row[j]];
      }
      x.emplace_back(env, 0, tagOf(serial, k, 0), ILOFLOAT);
      if (pairs && seen.emplace(pairOf(row), static_cast<std::uint32_t>(pair_keys.size())).second) {
        pair_keys.push_back(pairOf(row));
        y.emplace_back(env, 0, tagOf(std::array<std::uint32_t, 2>{serial[0], serial[last]}, 2, 1), ILOFLOAT);
      }
    }
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
    if (pairs) {
      iy.reserve(pair_keys.size());
      for (std::uint32_t i = 0; i < pair_keys.size(); ++i) {
        iy.add(pair_keys[i], i);
      }
    }
  });
  // The id of the period before each period, by id, as a hand-coder keeps it beside the entities.
  std::vector<std::uint32_t> prev_id(d.ents[last].size(), Index::kNone);
  for (const auto& e : d.ents[last]) {
    const Ent* p  = pairs ? d.prev(d.handles[last][e.serial]) : nullptr;
    prev_id[e.id] = p ? p->id : Index::kNone;
  }
  if (pairs) {
    r.measure("neighbor", [&] {
      std::uint64_t sum = 0;
      for (std::size_t i = 0; i < d.rows; ++i) {
        std::array<std::uint32_t, kMaxColumns> id{};
        for (std::size_t j = 0; j < k; ++j) {
          id[j] = ids[i * k + j];
        }
        const std::uint32_t p = prev_id[id[last]];
        if (p == Index::kNone) { continue; }
        id[last]              = p;
        const std::uint32_t e = ix.find(px(id));
        if (e != Index::kNone) { sum += ubOf(x[e]); }
      }
      r.check("neighbor", sum);
    });
    r.measure("project", [&] {
      std::uint64_t sum = 0;
      for (std::size_t i = 0; i < d.rows; ++i) {
        sum += ubOf(y[iy.find(pairOf(&ids[i * k]))]);
      }
      r.check("project", sum);
    });
  }
  const auto idsOf = [k](const std::array<const Ent*, kMaxColumns>& key) {
    std::array<std::uint32_t, kMaxColumns> id{};
    for (std::size_t j = 0; j < k; ++j) {
      id[j] = key[j]->id;
    }
    return id;
  };
  r.measure("hit", [&] {
    std::uint64_t sum = 0;
    for (const auto& key : s.hit) {
      sum += ubOf(x[ix.find(px(idsOf(key)))]);
    }
    r.check("hit", sum);
  });
  r.measure("miss", [&] {
    std::uint64_t found = 0;
    for (const auto& key : s.miss) {
      found += ix.find(px(idsOf(key))) != Index::kNone;
    }
    r.check("miss", found);
  });
  r.measure("half", [&] {
    std::uint64_t sum = 0;
    for (const auto& key : s.half) {
      const std::uint32_t e = ix.find(px(idsOf(key)));
      if (e != Index::kNone) { sum += ubOf(x[e]); }
    }
    r.check("half", sum);
  });
  env.end();
}

// The map arm, the usual code: one std::map from a row's entities to its variable.
static void runMap(const Data& d, const Samples& s, Result& r) {
  using Key           = std::array<const Ent*, kMaxColumns>;  // The columns past k stay null.
  const std::size_t k = d.k, last = d.last();
  const bool pairs = k > 1;  // Whether the cell has y. A cell at k = 1 has no period, so it has no y.
  IloEnv env;
  std::map<Key, IloNumVar> x;
  std::map<std::pair<const Ent*, const Ent*>, IloNumVar> y;
  std::vector<Key> rows;  // The rows in generation order, so that the phases walk them as the other arms do.
  r.measure("build", [&] {
    rows.reserve(d.rows);
    d.forEachRow([&](const Key& row) {
      Key key{};
      std::copy_n(row.begin(), k, key.begin());
      rows.push_back(key);
      x.emplace(key, IloNumVar(env, 0, tagOf(serialsOf(key, k), k, 0), ILOFLOAT));
      const auto pair = std::make_pair(key[0], key[last]);
      if (pairs && !y.contains(pair)) {
        y.emplace(
            pair,
            IloNumVar(env, 0, tagOf(std::array<std::uint32_t, 2>{key[0]->serial, key[last]->serial}, 2, 1), ILOFLOAT));
      }
    });
  });
  r.measure("index", [] {});
  if (pairs) {
    r.measure("neighbor", [&] {
      std::uint64_t sum = 0;
      for (Key key : rows) {
        const Ent* p = d.prev(key[last]);
        if (p == nullptr) { continue; }
        key[last] = p;
        if (const auto it = x.find(key); it != x.end()) { sum += ubOf(it->second); }
      }
      r.check("neighbor", sum);
    });
    r.measure("project", [&] {
      std::uint64_t sum = 0;
      for (const Key& key : rows) {
        sum += ubOf(y.at({key[0], key[last]}));
      }
      r.check("project", sum);
    });
  }
  const auto keyOf = [k](const Key& sample) {
    Key key{};
    std::copy_n(sample.begin(), k, key.begin());
    return key;
  };
  r.measure("hit", [&] {
    std::uint64_t sum = 0;
    for (const auto& sample : s.hit) {
      sum += ubOf(x.at(keyOf(sample)));
    }
    r.check("hit", sum);
  });
  r.measure("miss", [&] {
    std::uint64_t found = 0;
    for (const auto& sample : s.miss) {
      found += x.contains(keyOf(sample));
    }
    r.check("miss", found);
  });
  r.measure("half", [&] {
    std::uint64_t sum = 0;
    for (const auto& sample : s.half) {
      if (const auto it = x.find(keyOf(sample)); it != x.end()) { sum += ubOf(it->second); }
    }
    r.check("half", sum);
  });
  env.end();
}

static void run(const std::string& impl, const Data& d, const Samples& s, Result& r) {
  if (impl == "identity") {
    runHand<MapIndex<std::hash<std::uint64_t>>>(d, s, r);
  } else if (impl == "mixed") {
    runHand<MapIndex<MixedHash>>(d, s, r);
  } else if (impl == "array") {
    runHand<ArrayIndex>(d, s, r);
  } else if (impl == "map") {
    runMap(d, s, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace lookup
