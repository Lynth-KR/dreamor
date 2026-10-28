// SPDX-License-Identifier: Apache-2.0
// catalog/slice.hpp - the slice family: groups reached from another set's keys, one sum a key
//
// Two shapes, as in group: inner (every column but the period: the groups are runs) and lead
// (every column but k1). Each shape has a driver, the set a model walks to write one expression a
// key - a balance over ITEMS x PERIODS, say. It holds every key of the shape and, beside each, one
// no row has (the key's last column swapped for an entity of its own), the two in a random order:
// half the keys reach an empty slice, as some (item, period) pairs have no production.
// Phases, the same in every arm: build (the rows and x on them), then for each shape prep_<shape>
// (the grouping and what a key reaches its group through) and slice_<shape> (one expression a
// driver key, x over its group's rows); and on the inner shape split (two expressions a key, its
// rows in even and odd periods, in one pass) and coef (one expression a key, c(t) * x a row).
//
// dreamor's groupBy, slice and sumOf are set against hand-written groupings, as in group, each
// with what a key goes through: the group numbers in a std::unordered_map from the packed key
// (`identity`, `mixed`); the packed keys sorted, searched with std::lower_bound (`sort`); and the
// usual code, a std::map from the key's entities to its rows (`map`). The packed arms take the
// inner shape's groups as runs.
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
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "group.hpp"
#include "shared.hpp"

namespace slice {

using catalog::consume;
using catalog::keyOf;
using catalog::MixedHash;
using catalog::Packer;
using Key = std::array<const Ent*, kMaxColumns>;  // a shape's columns at the front

inline double coefOf(std::uint32_t period_serial) {
  return 1.0 + static_cast<double>(period_serial % 7);
}

// A shape's driver: its columns, its keys in the order a model walks them, and the entities of its
// last column that no row holds, one for each that rows do.
struct Driver {
  std::string shape;
  std::vector<std::size_t> kept;
  std::vector<Key> keys;
  std::vector<std::unique_ptr<Ent>> shadows;
};
using Drivers = std::array<Driver, 2>;  // inner, lead

inline std::uint64_t keyCheck(const Key& key, std::size_t m) {
  std::array<std::uint32_t, kMaxColumns> s{};
  for (std::size_t i = 0; i < m; ++i) {
    s[i] = key[i]->serial;
  }
  return keyOf(s, m);
}

static Driver makeDriver(const Data& d, const std::string& shape, std::uint64_t seed) {
  Driver dr;
  dr.shape                = shape;
  dr.kept                 = group::keptBy(shape, d.k);
  const std::size_t m     = dr.kept.size();
  const std::size_t count = d.ents[dr.kept.back()].size();
  const bool inner        = shape == "inner";
  dr.shadows.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto s = static_cast<std::uint32_t>(count + i);
    dr.shadows.push_back(std::make_unique<Ent>(Ent{s, s}));
  }
  // The distinct keys, found through an open-addressing table of their places in `distinct`.
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
  d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
    Key key{};
    for (std::size_t i = 0; i < m; ++i) {
      key[i] = row[dr.kept[i]];
    }
    if (inner ? key == previous : !fresh(key)) {
      return;  // inner keys come as runs
    }
    previous      = key;
    Key absent    = key;
    absent[m - 1] = dr.shadows[key[m - 1]->serial].get();
    if ((g() & 1) != 0) { std::swap(key, absent); }
    dr.keys.push_back(key);
    dr.keys.push_back(absent);
  });
  return dr;
}

// --- dreamor ---------------------------------------------------------------------------------
template <typename Seq>
struct Dreamor;
template <std::size_t... J>
struct Dreamor<std::index_sequence<J...>> {
  using G                            = catalog::Gen<std::index_sequence<J...>>;
  static constexpr std::size_t kK    = G::kK;
  static constexpr std::size_t kLast = G::kLast;
  using Set                          = typename G::Set;
  using Table                        = typename G::Table;

  struct Even {
    bool operator()(KCol<kLast> t) const { return group::evenPeriod(t.get()); }
  };
  struct Odd {
    bool operator()(KCol<kLast> t) const { return !group::evenPeriod(t.get()); }
  };
  // A term on the row's columns, as a model writes cost[t] * x[...]: x is found from the row being summed.
  struct Coef {
    const Table* x;
    IloNumExprArg operator()(KCol<J>... c) const {
      const std::array<const Ent*, kK> e{c.get()...};
      return coefOf(e[kLast]->serial) * (*x)(c...);
    }
  };

  static void run(const Data& d, const Drivers& drivers, Result& r) {
    IloEnv env;
    Set sX;
    std::optional<Table> vX;
    r.measure("build", [&] {
      sX = G::make(d);
      vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT);
    });
    shape(drivers[0], std::make_index_sequence<kK - 1>{}, sX, *vX, env, r);
    shape(drivers[1], decltype(group::shifted<1>(std::make_index_sequence<kK - 1>{})){}, sX, *vX, env, r);
    env.end();
  }

  template <std::size_t... I>
  static void shape(
      const Driver& dr, std::index_sequence<I...>, const Set& sX, const Table& vX, IloEnv env, Result& r) {
    using Row = std::tuple<KCol<I>...>;
    std::vector<Row> rows;
    rows.reserve(dr.keys.size());
    [&]<std::size_t... P>(std::index_sequence<P...>) {
      for (const Key& key : dr.keys) {
        rows.emplace_back(KCol<I>{key[P]}...);
      }
    }(std::make_index_sequence<sizeof...(I)>{});
    const auto sDriver = dreamor::makeSet<>(rows).collect();
    const auto check   = [](const Row& key) {
      return keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<KCol<I>>(key)->serial...}, sizeof...(I));
    };

    using Grouped = decltype(sX.template groupBy<KCol<I>...>());
    std::optional<Grouped> groups;
    r.measure("prep_" + dr.shape, [&] { groups.emplace(sX.template groupBy<KCol<I>...>()); });
    r.measure("slice_" + dr.shape, [&] {
      std::uint64_t sum = 0;
      for (const auto& key : sDriver) {
        IloExpr e = dreamor::concert::sumOf(env, groups->slice(key), vX).build();
        sum += consume(e, check(key));
      }
      r.check("slice_" + dr.shape, sum);
    });
    if (dr.shape != "inner") { return; }
    r.measure("split", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : sDriver) {
        auto parts            = dreamor::concert::sumOf(env, groups->slice(key), vX).split(Even{}, Odd{}).build();
        const std::uint64_t h = check(key);
        sum += consume(parts[0], h) + consume(parts[1], ~h);
      }
      r.check("split", sum);
    });
    r.measure("coef", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : sDriver) {
        IloExpr e = dreamor::concert::sumOf(env, groups->slice(key), Coef{&vX}).build();
        sum += consume(e, check(key));
      }
      r.check("coef", sum);
    });
  }
};

// --- hand-written ----------------------------------------------------------------------------
enum class Way { kIdentity, kMixed, kSort, kMap };

static void runHand(Way way, const Data& d, const Drivers& drivers, Result& r) {
  const std::size_t k = d.k, last = d.last();
  std::array<Packer, 2> packs;
  for (std::size_t s = 0; s < drivers.size(); ++s) {
    const std::vector<std::size_t>& kept = drivers[s].kept;
    packs[s].k                           = kept.size();
    for (std::size_t i = 0; i < kept.size(); ++i) {
      // The last column's ids go on past the entities, for the keys no row has.
      packs[s].size[i] = d.ents[kept[i]].size() * (i + 1 == kept.size() ? 2 : 1);
    }
    if (way != Way::kMap && packs[s].span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
      r.skip("the ids do not pack into 64 bits");
      return;
    }
  }
  IloEnv env;
  std::vector<std::uint32_t> ids;  // k a row
  std::vector<const Ent*> ents;    // k a row, for the map arm
  std::vector<IloNumVar> x;
  std::vector<std::uint32_t> period;  // each row's period serial
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

    // The groups - rows in group order and where each starts - and what a key goes through.
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
        std::sort(order.begin(), order.end());  // by key, then by row: rows keep their order
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

    // Calls f(row) for every row of key's group; an absent key has none.
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
  Drivers drivers;
  r.setup("drivers", [&] {
    drivers[0] = makeDriver(d, "inner", seed);
    drivers[1] = makeDriver(d, "lead", seed);
  });
  if (impl == "dreamor") {
    switch (d.k) {
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, drivers, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, drivers, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, drivers, r); break;
      default: r.skip("k must be 2, 3 or 5");
    }
  } else if (impl == "identity") {
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
