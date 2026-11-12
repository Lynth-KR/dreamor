// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/group.hpp - the hand-written arms of the group family, which ../group.hpp describes.
#pragma once

#include "../group.hpp"
#include "arm.hpp"

namespace group {

enum class Way { kIdentity, kMixed, kSort, kMap };

// The groups as a hand-written arm keeps them: the rows in group order, and where each group starts.
struct Groups {
  std::vector<std::uint32_t> rows, start;  // start ends with one past the end of the last group.
};

static void runHand(Way way, const Data& d, Result& r) {
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  Packer full;
  full.k = k;
  for (std::size_t j = 0; j < k; ++j) {
    full.size[j] = d.ents[j].size();
  }
  if (way != Way::kMap && full.span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    r.skip("the ids do not pack into 64 bits");
    env.end();
    return;
  }
  std::vector<std::uint32_t> ids;  // The ids of the rows, k for each row.
  std::vector<const Ent*> ents;    // The entities of the rows, k for each row, for the map arm.
  std::vector<IloNumVar> x;
  std::array<std::vector<std::uint32_t>, kMaxColumns> serial_of;  // The serial of each entity of column j, by its id.
  for (std::size_t j = 0; j < k; ++j) {
    serial_of[j].resize(d.ents[j].size());
    for (const auto& e : d.ents[j]) {
      serial_of[j][e.id] = e.serial;
    }
  }
  r.measure("build", [&] {
    ids.reserve(d.rows * k);
    if (way == Way::kMap) { ents.reserve(d.rows * k); }
    x.reserve(d.rows);
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      std::array<std::uint32_t, kMaxColumns> serial{};
      for (std::size_t j = 0; j < k; ++j) {
        ids.push_back(row[j]->id);
        serial[j] = row[j]->serial;
        if (way == Way::kMap) { ents.push_back(row[j]); }
      }
      x.emplace_back(env, 0, tagOf(serial, k, 0), ILOFLOAT);
    });
  });
  const auto groupKey = [&](std::size_t row, const std::vector<std::size_t>& kept) {
    std::array<std::uint32_t, kMaxColumns> s{};
    for (std::size_t i = 0; i < kept.size(); ++i) {
      s[i] = serial_of[kept[i]][ids[row * k + kept[i]]];
    }
    return keyOf(s, kept.size());
  };

  for (const std::string& shape : shapesFor(k)) {
    const std::vector<std::size_t> kept = keptBy(shape, k);
    Packer pack;
    pack.k = kept.size();
    for (std::size_t i = 0; i < kept.size(); ++i) {
      pack.size[i] = d.ents[kept[i]].size();
    }
    const auto packed = [&](std::size_t row) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t i = 0; i < kept.size(); ++i) {
        id[i] = ids[row * k + kept[i]];
      }
      return pack(id);
    };
    Groups g;
    r.measure("prep_" + shape, [&] {
      const std::size_t n = x.size();
      if (shape == "inner" && way != Way::kMap) {
        // The period is innermost, so a group's rows come one after another.
        for (std::size_t i = 0; i < n; ++i) {
          if (i == 0 || packed(i) != packed(i - 1)) { g.start.push_back(static_cast<std::uint32_t>(i)); }
        }
        g.rows.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
          g.rows[i] = i;
        }
      } else if (way == Way::kIdentity || way == Way::kMixed) {
        const auto csr = [&](auto& index) {
          std::vector<std::uint32_t> group_of(n), count;
          index.reserve(n);
          for (std::size_t i = 0; i < n; ++i) {
            const auto [it, fresh] = index.try_emplace(packed(i), static_cast<std::uint32_t>(count.size()));
            if (fresh) { count.push_back(0); }
            group_of[i] = it->second;
            ++count[it->second];
          }
          g.start.resize(count.size());
          std::uint32_t running = 0;
          for (std::size_t c = 0; c < count.size(); ++c) {
            g.start[c] = std::exchange(running, running + count[c]);
          }
          g.rows.resize(n);
          std::vector<std::uint32_t> next(g.start);
          for (std::uint32_t i = 0; i < n; ++i) {
            g.rows[next[group_of[i]]++] = i;
          }
        };
        if (way == Way::kIdentity) {
          std::unordered_map<std::uint64_t, std::uint32_t> index;
          csr(index);
        } else {
          std::unordered_map<std::uint64_t, std::uint32_t, MixedHash> index;
          csr(index);
        }
      } else if (way == Way::kSort) {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> order(n);
        for (std::uint32_t i = 0; i < n; ++i) {
          order[i] = {packed(i), i};
        }
        std::sort(order.begin(), order.end());  // By key, then by row, so a group's rows keep their order.
        g.rows.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
          g.rows[i] = order[i].second;
          if (i == 0 || order[i].first != order[i - 1].first) { g.start.push_back(static_cast<std::uint32_t>(i)); }
        }
      } else {
        std::map<std::array<const Ent*, kMaxColumns>, std::vector<std::uint32_t>> groups;
        for (std::uint32_t i = 0; i < n; ++i) {
          std::array<const Ent*, kMaxColumns> key{};
          for (std::size_t c = 0; c < kept.size(); ++c) {
            key[c] = ents[i * k + kept[c]];
          }
          groups[key].push_back(i);
        }
        for (const auto& [key, rows] : groups) {
          g.start.push_back(static_cast<std::uint32_t>(g.rows.size()));
          g.rows.insert(g.rows.end(), rows.begin(), rows.end());
        }
      }
      g.start.push_back(static_cast<std::uint32_t>(n));
    });
    const auto sum = [&](const std::string& phase, bool even_only) {
      r.measure(phase, [&] {
        std::uint64_t check = 0;
        for (std::size_t c = 0; c + 1 < g.start.size(); ++c) {
          IloExpr e(env);
          for (std::uint32_t p = g.start[c]; p < g.start[c + 1]; ++p) {
            const std::uint32_t i = g.rows[p];
            if (even_only && serial_of[last][ids[i * k + last]] % 2 != 0) { continue; }
            e += x[i];
          }
          // The hash of the group's key, from the group's first row. dreamor's arm reads the same key from the group.
          check += consume(e, groupKey(g.rows[g.start[c]], kept));
        }
        r.check(phase, check);
      });
    };
    sum("sum_" + shape, false);
    if (shape == "inner") { sum("sum_where", true); }
  }
  env.end();
}

static void run(const std::string& impl, const Data& d, const Samples&, Result& r) {
  if (d.k == 1) {
    r.skip("no k = 1: the family's operations need a period beside k1");
    return;
  }
  if (impl == "identity") {
    runHand(Way::kIdentity, d, r);
  } else if (impl == "mixed") {
    runHand(Way::kMixed, d, r);
  } else if (impl == "sort") {
    runHand(Way::kSort, d, r);
  } else if (impl == "map") {
    runHand(Way::kMap, d, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace group
