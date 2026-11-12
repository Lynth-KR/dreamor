// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/slice.hpp - the hand-written arms of the slice family, which ../slice.hpp describes.
#pragma once

#include "../slice.hpp"
#include "arm.hpp"

namespace slice {

using Key     = KeyOf<HandArm>;
using Driver  = DriverOf<HandArm>;
using Drivers = DriversOf<HandArm>;

enum class Way { kIdentity, kMixed, kSort, kMap };

static void runHand(Way way, const Data& d, const Drivers& drivers, Result& r) {
  const std::size_t k = d.k, last = d.last();
  std::array<Packer, 2> packs;
  for (std::size_t s = 0; s < drivers.size(); ++s) {
    const std::vector<std::size_t>& kept = drivers[s].kept;
    packs[s].k                           = kept.size();
    for (std::size_t i = 0; i < kept.size(); ++i) {
      // The ids of the last column go on past its entities, for the keys that no row has.
      packs[s].size[i] = d.ents[kept[i]].size() * (i + 1 == kept.size() ? 2 : 1);
    }
    if (way != Way::kMap && packs[s].span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
      r.skip("the ids do not pack into 64 bits");
      return;
    }
  }
  IloEnv env;
  std::vector<std::uint32_t> ids;  // The ids of the rows, k for each row.
  std::vector<const Ent*> ents;    // The entities of the rows, k for each row, for the map arm.
  std::vector<IloNumVar> x;
  std::vector<std::uint32_t> period;  // The serial of each row's period.
  r.measure("build", [&] {
    ids.reserve(d.rows * k);
    if (way == Way::kMap) { ents.reserve(d.rows * k); }
    x.reserve(d.rows);
    period.reserve(d.rows);
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      std::array<std::uint32_t, kMaxColumns> serial{};
      for (std::size_t j = 0; j < k; ++j) {
        ids.push_back(row[j]->id);
        serial[j] = row[j]->serial;
        if (way == Way::kMap) { ents.push_back(row[j]); }
      }
      x.emplace_back(env, 0, tagOf(serial, k, 0), ILOFLOAT);
      period.push_back(serial[last]);
    });
  });

  for (std::size_t s = 0; s < drivers.size(); ++s) {
    const Driver& dr                     = drivers[s];
    const std::vector<std::size_t>& kept = dr.kept;
    const std::size_t m                  = kept.size();
    const Packer& pack                   = packs[s];
    const auto packedRow                 = [&](std::size_t row) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t i = 0; i < m; ++i) {
        id[i] = ids[row * k + kept[i]];
      }
      return pack(id);
    };
    const auto packedKey = [&](const Key& key) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t i = 0; i < m; ++i) {
        id[i] = key[i]->id;
      }
      return pack(id);
    };

    // The groups, as the rows in group order and where each group starts, and the maps that a key goes through.
    std::vector<std::uint32_t> rows, start;
    std::unordered_map<std::uint64_t, std::uint32_t> by_identity;
    std::unordered_map<std::uint64_t, std::uint32_t, MixedHash> by_mixed;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> by_sorted;
    std::map<Key, std::vector<std::uint32_t>> by_map;
    const auto addGroup = [&](std::uint64_t key, std::uint32_t group) {
      if (way == Way::kIdentity) { by_identity.emplace(key, group); }
      if (way == Way::kMixed) { by_mixed.emplace(key, group); }
      if (way == Way::kSort) { by_sorted.emplace_back(key, group); }
    };
    r.measure("prep_" + dr.shape, [&] {
      const std::size_t n = x.size();
      if (way == Way::kMap) {
        for (std::uint32_t i = 0; i < n; ++i) {
          Key key{};
          for (std::size_t c = 0; c < m; ++c) {
            key[c] = ents[i * k + kept[c]];
          }
          by_map[key].push_back(i);
        }
        return;
      }
      if (dr.shape == "inner") {
        // The period is innermost, so a group's rows come one after another.
        if (way == Way::kIdentity) { by_identity.reserve(n); }
        if (way == Way::kMixed) { by_mixed.reserve(n); }
        std::uint64_t previous = 0;
        for (std::size_t i = 0; i < n; ++i) {
          const std::uint64_t key = packedRow(i);
          if (i == 0 || key != previous) {
            addGroup(key, static_cast<std::uint32_t>(start.size()));
            start.push_back(static_cast<std::uint32_t>(i));
            previous = key;
          }
        }
        rows.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
          rows[i] = i;
        }
        if (way == Way::kSort) { std::sort(by_sorted.begin(), by_sorted.end()); }
      } else if (way == Way::kSort) {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> order(n);
        for (std::uint32_t i = 0; i < n; ++i) {
          order[i] = {packedRow(i), i};
        }
        std::sort(order.begin(), order.end());  // By key, then by row, so a group's rows keep their order.
        rows.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
          rows[i] = order[i].second;
          if (i == 0 || order[i].first != order[i - 1].first) {
            by_sorted.emplace_back(order[i].first, static_cast<std::uint32_t>(start.size()));
            start.push_back(static_cast<std::uint32_t>(i));
          }
        }
      } else {
        const auto csr = [&](auto& index) {
          std::vector<std::uint32_t> group_of(n), count;
          index.reserve(n);
          for (std::size_t i = 0; i < n; ++i) {
            const auto [it, fresh] = index.try_emplace(packedRow(i), static_cast<std::uint32_t>(count.size()));
            if (fresh) { count.push_back(0); }
            group_of[i] = it->second;
            ++count[it->second];
          }
          start.resize(count.size());
          std::uint32_t running = 0;
          for (std::size_t c = 0; c < count.size(); ++c) {
            start[c] = std::exchange(running, running + count[c]);
          }
          rows.resize(n);
          std::vector<std::uint32_t> next(start);
          for (std::uint32_t i = 0; i < n; ++i) {
            rows[next[group_of[i]]++] = i;
          }
        };
        if (way == Way::kIdentity) {
          csr(by_identity);
        } else {
          csr(by_mixed);
        }
      }
      start.push_back(static_cast<std::uint32_t>(n));
    });

    // Calls f(row) for every row of the group of key. A key that no row has has no group, so f is not called.
    const auto forRows = [&](const Key& key, auto&& f) {
      if (way == Way::kMap) {
        const auto it = by_map.find(key);
        if (it != by_map.end()) {
          for (const std::uint32_t i : it->second) {
            f(i);
          }
        }
        return;
      }
      std::uint32_t group        = 0;
      const std::uint64_t packed = packedKey(key);
      if (way == Way::kSort) {
        const auto it = std::lower_bound(by_sorted.begin(), by_sorted.end(), std::pair{packed, std::uint32_t{0}});
        if (it == by_sorted.end() || it->first != packed) { return; }
        group = it->second;
      } else if (way == Way::kIdentity) {
        const auto it = by_identity.find(packed);
        if (it == by_identity.end()) { return; }
        group = it->second;
      } else {
        const auto it = by_mixed.find(packed);
        if (it == by_mixed.end()) { return; }
        group = it->second;
      }
      for (std::uint32_t p = start[group]; p < start[group + 1]; ++p) {
        f(rows[p]);
      }
    };

    r.measure("slice_" + dr.shape, [&] {
      std::uint64_t sum = 0;
      for (const Key& key : dr.keys) {
        IloExpr e(env);
        forRows(key, [&](std::uint32_t i) { e += x[i]; });
        sum += consume(e, keyCheck(key, m));
      }
      r.check("slice_" + dr.shape, sum);
    });
    if (dr.shape != "inner") { continue; }
    r.measure("split", [&] {
      std::uint64_t sum = 0;
      for (const Key& key : dr.keys) {
        IloExpr even(env), odd(env);
        forRows(key, [&](std::uint32_t i) { (period[i] % 2 == 0 ? even : odd) += x[i]; });
        const std::uint64_t h = keyCheck(key, m);
        sum += consume(even, h) + consume(odd, ~h);
      }
      r.check("split", sum);
    });
    r.measure("coef", [&] {
      std::uint64_t sum = 0;
      for (const Key& key : dr.keys) {
        IloExpr e(env);
        forRows(key, [&](std::uint32_t i) { e += coefOf(period[i]) * x[i]; });
        sum += consume(e, keyCheck(key, m));
      }
      r.check("coef", sum);
    });
  }
  env.end();
}

static void run(const std::string& impl, const Data& d, std::uint64_t seed, Result& r) {
  if (d.k == 1) {
    r.skip("no k = 1: the family's operations need a period beside k1");
    return;
  }
  Drivers drivers;
  r.setup("drivers", [&] {
    drivers[0] = makeDriver(d, "inner", seed);
    drivers[1] = makeDriver(d, "lead", seed);
  });
  if (impl == "identity") {
    runHand(Way::kIdentity, d, drivers, r);
  } else if (impl == "mixed") {
    runHand(Way::kMixed, d, drivers, r);
  } else if (impl == "sort") {
    runHand(Way::kSort, d, drivers, r);
  } else if (impl == "map") {
    runHand(Way::kMap, d, drivers, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace slice
