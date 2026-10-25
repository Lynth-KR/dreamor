// SPDX-License-Identifier: Apache-2.0
// catalog/lookup.hpp - the lookup family: a variable found by the columns of its key
//
// Phases, the same in every arm:
//   build     the rows, x on them, the (k1, period) pairs the rows have, y on those
//   index     what the lookups below go through: dreamor's row indexes of x and y (built by the
//             first lookup that needs them, forced here), a hand-written arm's map or array
//   neighbor  every row's x at the period before, if that row exists: contains, then the lookup
//   project   every row's y at its (k1, period): a family on fewer columns
//   hit       x at keys drawn at random from the rows
//   miss      contains() of the same keys with the period in no row
//   half      contains(), then x where it is there, over keys of which half are rows
//
// dreamor is set against hand-written arms that pack the ids into one integer and keep rows in a
// vector: through std::unordered_map with libstdc++'s identity hash (`identity`), with a mixed
// hash (`mixed`), or through a direct array over every combination of ids (`array`), which a
// hand-coder takes when the ids are dense and their product fits in memory. Packing needs the
// product of the id counts to fit in 64 bits; where it does not, those arms do not run. `map` is
// the usual hand-written code, which runs everywhere: one std::map from a row's entities to its
// variable, filled as the rows are generated, as examples/production_planning_plain.hpp does; it
// has no index of its own, so its `index` phase is empty.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "shared.hpp"

namespace lookup {

using catalog::MixedHash;
using catalog::Packer;
using catalog::serialsOf;
using catalog::ubOf;

inline const std::vector<std::string> kPhases = {"build", "index", "neighbor", "project", "hit", "miss", "half"};

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
  using PairSet                      = dreamor::SetOf<KCol<0>, KCol<kLast>>;
  using PairTable                    = dreamor::concert::VarTable<KCol<0>, KCol<kLast>>;

  struct PairTag {
    double operator()(KCol<0> a, KCol<kLast> t) const {
      return tagOf(std::array<std::uint32_t, 2>{a->serial, t->serial}, 2, 1);
    }
  };

  static void run(const Data& d, const Samples& s, Result& r) {
    IloEnv env;
    Set sX;
    PairSet sY;
    std::optional<Table> vX;
    std::optional<PairTable> vY;
    r.measure("build", [&] {
      sX = G::make(d);
      vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT);
      sY = sX.template select<KCol<0>, KCol<kLast>>();
      vY.emplace(env, sY, 0, PairTag{}, ILOFLOAT);
    });
    r.measure("index", [&] {
      // An absent key: the lookup has to build the index to say so.
      const auto& row = *sX.begin();
      r.sink += vX->contains(replaced<J>(row, d.absent)...);
      r.sink += vY->contains(std::get<KCol<0>>(row), KCol<kLast>{d.absent});
    });
    r.measure("neighbor", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        const Ent* p = d.prev(std::get<KCol<kLast>>(row).get());
        if (p == nullptr) continue;
        const auto key = std::tuple{replaced<J>(row, p)...};
        if (std::apply([&](auto... c) { return vX->contains(c...); }, key)) {
          sum += ubOf(std::apply([&](auto... c) { return (*vX)(c...); }, key));
        }
      }
      r.check("neighbor", sum);
    });
    r.measure("project", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) sum += ubOf((*vY)(std::get<KCol<0>>(row), std::get<KCol<kLast>>(row)));
      r.check("project", sum);
    });
    r.measure("hit", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : s.hit) sum += ubOf((*vX)(KCol<J>{key[J]}...));
      r.check("hit", sum);
    });
    r.measure("miss", [&] {
      std::uint64_t found = 0;
      for (const auto& key : s.miss) found += vX->contains(KCol<J>{key[J]}...);
      r.check("miss", found);
    });
    r.measure("half", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : s.half) {
        if (vX->contains(KCol<J>{key[J]}...)) sum += ubOf((*vX)(KCol<J>{key[J]}...));
      }
      r.check("half", sum);
    });
    env.end();
  }

  // Column I of row, with the period replaced by p.
  template <std::size_t I, typename Row>
  static KCol<I> replaced(const Row& row, const Ent* p) {
    if constexpr (I == kLast) {
      return KCol<I>{p};
    } else {
      return std::get<KCol<I>>(row);
    }
  }
};

// --- hand-written ----------------------------------------------------------------------------
// The index a hand-written arm keeps from a packed key to an entry.
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
struct ArrayIndex {
  std::vector<std::uint32_t> slot;
  std::size_t span = 0;
  void reserve(std::size_t) { slot.assign(span, kNone); }
  void add(std::uint64_t key, std::uint32_t entry) { slot[key] = entry; }
  [[nodiscard]] std::uint32_t find(std::uint64_t key) const { return slot[key]; }
  static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
};

// A direct array is what a hand-coder writes only while it fits: at most this many slots a row.
inline constexpr long double kArraySlotsPerRow = 16;

template <typename Index>
static void runHand(const Data& d, const Samples& s, Result& r) {
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  Packer px, py;
  px.k = k;
  for (std::size_t j = 0; j < k; ++j) px.size[j] = d.ents[j].size();
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

  std::vector<std::uint32_t> ids;  // k a row
  std::vector<IloNumVar> x, y;
  std::vector<std::uint64_t> pair_keys;  // y's keys, packed (k1, period)
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
      for (std::size_t j = 0; j < k; ++j) ids.push_back(row[j]->id);
    });
    x.reserve(d.rows);
    // Tags need serials; ids map back to them through a table per column.
    std::array<std::vector<std::uint32_t>, kMaxColumns> serial_of;
    for (std::size_t j = 0; j < k; ++j) {
      serial_of[j].resize(d.ents[j].size());
      for (const auto& e : d.ents[j]) serial_of[j][e->id] = e->serial;
    }
    std::unordered_map<std::uint64_t, std::uint32_t, MixedHash> seen;  // pairs in first-appearance order
    for (std::size_t i = 0; i < d.rows; ++i) {
      const std::uint32_t* row = &ids[i * k];
      std::array<std::uint32_t, kMaxColumns> serial{};
      for (std::size_t j = 0; j < k; ++j) serial[j] = serial_of[j][row[j]];
      x.emplace_back(env, 0, tagOf(serial, k, 0), ILOFLOAT);
      if (seen.emplace(pairOf(row), static_cast<std::uint32_t>(pair_keys.size())).second) {
        pair_keys.push_back(pairOf(row));
        y.emplace_back(env, 0, tagOf(std::array<std::uint32_t, 2>{serial[0], serial[last]}, 2, 1), ILOFLOAT);
      }
    }
  });
  r.measure("index", [&] {
    ix.reserve(d.rows);
    for (std::uint32_t i = 0; i < d.rows; ++i) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t j = 0; j < k; ++j) id[j] = ids[i * k + j];
      ix.add(px(id), i);
    }
    iy.reserve(pair_keys.size());
    for (std::uint32_t i = 0; i < pair_keys.size(); ++i) iy.add(pair_keys[i], i);
  });
  // The period before, by id: what a hand-coder keeps beside the entities.
  std::vector<std::uint32_t> prev_id(d.ents[last].size(), Index::kNone);
  for (const auto& e : d.ents[last]) {
    const Ent* p   = d.prev(e.get());
    prev_id[e->id] = p ? p->id : Index::kNone;
  }
  r.measure("neighbor", [&] {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < d.rows; ++i) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t j = 0; j < k; ++j) id[j] = ids[i * k + j];
      const std::uint32_t p = prev_id[id[last]];
      if (p == Index::kNone) continue;
      id[last]              = p;
      const std::uint32_t e = ix.find(px(id));
      if (e != Index::kNone) sum += ubOf(x[e]);
    }
    r.check("neighbor", sum);
  });
  r.measure("project", [&] {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < d.rows; ++i) sum += ubOf(y[iy.find(pairOf(&ids[i * k]))]);
    r.check("project", sum);
  });
  const auto idsOf = [k](const std::array<const Ent*, kMaxColumns>& key) {
    std::array<std::uint32_t, kMaxColumns> id{};
    for (std::size_t j = 0; j < k; ++j) id[j] = key[j]->id;
    return id;
  };
  r.measure("hit", [&] {
    std::uint64_t sum = 0;
    for (const auto& key : s.hit) sum += ubOf(x[ix.find(px(idsOf(key)))]);
    r.check("hit", sum);
  });
  r.measure("miss", [&] {
    std::uint64_t found = 0;
    for (const auto& key : s.miss) found += ix.find(px(idsOf(key))) != Index::kNone;
    r.check("miss", found);
  });
  r.measure("half", [&] {
    std::uint64_t sum = 0;
    for (const auto& key : s.half) {
      const std::uint32_t e = ix.find(px(idsOf(key)));
      if (e != Index::kNone) sum += ubOf(x[e]);
    }
    r.check("half", sum);
  });
  env.end();
}

// --- the usual hand-written code --------------------------------------------------------------
static void runMap(const Data& d, const Samples& s, Result& r) {
  using Key           = std::array<const Ent*, kMaxColumns>;  // unused columns stay null
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  std::map<Key, IloNumVar> x;
  std::map<std::pair<const Ent*, const Ent*>, IloNumVar> y;
  std::vector<Key> rows;  // the rows in generation order, to walk them as the other arms do
  r.measure("build", [&] {
    rows.reserve(d.rows);
    d.forEachRow([&](const Key& row) {
      Key key{};
      std::copy_n(row.begin(), k, key.begin());
      rows.push_back(key);
      x.emplace(key, IloNumVar(env, 0, tagOf(serialsOf(key, k), k, 0), ILOFLOAT));
      const auto pair = std::make_pair(key[0], key[last]);
      if (!y.contains(pair)) {
        y.emplace(
            pair,
            IloNumVar(env, 0, tagOf(std::array<std::uint32_t, 2>{key[0]->serial, key[last]->serial}, 2, 1), ILOFLOAT));
      }
    });
  });
  r.measure("index", [] {});
  r.measure("neighbor", [&] {
    std::uint64_t sum = 0;
    for (Key key : rows) {
      const Ent* p = d.prev(key[last]);
      if (p == nullptr) continue;
      key[last] = p;
      if (const auto it = x.find(key); it != x.end()) sum += ubOf(it->second);
    }
    r.check("neighbor", sum);
  });
  r.measure("project", [&] {
    std::uint64_t sum = 0;
    for (const Key& key : rows) sum += ubOf(y.at({key[0], key[last]}));
    r.check("project", sum);
  });
  const auto keyOf = [k](const Key& sample) {
    Key key{};
    std::copy_n(sample.begin(), k, key.begin());
    return key;
  };
  r.measure("hit", [&] {
    std::uint64_t sum = 0;
    for (const auto& sample : s.hit) sum += ubOf(x.at(keyOf(sample)));
    r.check("hit", sum);
  });
  r.measure("miss", [&] {
    std::uint64_t found = 0;
    for (const auto& sample : s.miss) found += x.contains(keyOf(sample));
    r.check("miss", found);
  });
  r.measure("half", [&] {
    std::uint64_t sum = 0;
    for (const auto& sample : s.half) {
      if (const auto it = x.find(keyOf(sample)); it != x.end()) sum += ubOf(it->second);
    }
    r.check("half", sum);
  });
  env.end();
}

static void run(const std::string& impl, const Data& d, const Samples& s, Result& r) {
  if (impl == "dreamor") {
    switch (d.k) {
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, s, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, s, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, s, r); break;
      default: r.skip("k must be 2, 3 or 5");
    }
  } else if (impl == "identity") {
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
