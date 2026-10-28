// SPDX-License-Identifier: Apache-2.0
// catalog/order.hpp - the order family: a period's neighbours, and the variables there
//
// Phases, the same in every arm:
//   build   the rows and x on them
//   index   what the phases below go through: dreamor's row index of x (forced) and the periods'
//           Ordered, a hand-written arm's map or array (the periods' positions it keeps beside
//           the entities, as a hand-coder does, are made before any phase)
//   step    every row's period before (prev): the order alone, no variable
//   lag     every row's x at the period kLag before, if that row exists: x[..., t - L]
//   window  every row's x over the kWindow periods up to its own, those rows that exist: a
//           rolling sum, sum over tau in t-W+1..t of x[..., tau]
//
// dreamor's Ordered over the periods is set against the period's position, which a hand-coder
// keeps beside each entity, and arithmetic on it; the variables are found as in lookup (packed
// ids through std::unordered_map with the identity hash `identity` or a mixed one `mixed`, a
// direct array `array`, and the usual code, one std::map from a row's entities to its variable,
// `map`).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "lookup.hpp"
#include "shared.hpp"

namespace order {

using catalog::MixedHash;
using catalog::Packer;
using catalog::serialsOf;
using catalog::ubOf;

inline constexpr std::ptrdiff_t kLag    = 4;
inline constexpr std::ptrdiff_t kWindow = 4;

// Every period a list can hold, in order: the last column's entities but the absent one.
inline std::vector<const Ent*> periodsOf(const Data& d) {
  std::vector<const Ent*> periods;
  for (const auto& e : d.ents[d.last()]) {
    if (e.get() != d.absent) { periods.push_back(e.get()); }
  }
  return periods;
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

  // Column I of row, with the period replaced by p.
  template <std::size_t I, typename Row>
  static KCol<I> replaced(const Row& row, const Ent* p) {
    if constexpr (I == kLast) {
      return KCol<I>{p};
    } else {
      return std::get<KCol<I>>(row);
    }
  }

  static void run(const Data& d, Result& r) {
    IloEnv env;
    const std::vector<const Ent*> periods = periodsOf(d);
    const auto sPERIODS                   = dreamor::makeSet<KCol<kLast>>(periods).collect();
    Set sX;
    std::optional<Table> vX;
    r.measure("build", [&] {
      sX = G::make(d);
      vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT);
    });
    using Ordered = decltype(sPERIODS.ordered());
    std::optional<Ordered> ordered;
    r.measure("index", [&] {
      // An absent key: the lookup has to build the index to say so.
      r.sink += vX->contains(replaced<J>(*sX.begin(), d.absent)...);
      ordered.emplace(sPERIODS.ordered());
    });
    // x at row's columns with the period p, if that row exists.
    const auto at = [&](const auto& row, const Ent* p) -> std::uint64_t {
      const auto key = std::tuple{replaced<J>(row, p)...};
      if (!std::apply([&](auto... c) { return vX->contains(c...); }, key)) { return 0; }
      return ubOf(std::apply([&](auto... c) { return (*vX)(c...); }, key));
    };
    r.measure("step", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        const auto* p = ordered->prev(std::get<KCol<kLast>>(row));
        if (p != nullptr) { sum += std::get<0>(*p)->serial + 1; }
      }
      r.check("step", sum);
    });
    r.measure("lag", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        const auto* p = ordered->prev(std::get<KCol<kLast>>(row), kLag);
        if (p != nullptr) { sum += at(row, std::get<0>(*p).get()); }
      }
      r.check("lag", sum);
    });
    r.measure("window", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        for (std::ptrdiff_t l = 0; l < kWindow; ++l) {
          const auto* p = ordered->prev(std::get<KCol<kLast>>(row), l);
          if (p == nullptr) { break; }
          sum += at(row, std::get<0>(*p).get());
        }
      }
      r.check("window", sum);
    });
    env.end();
  }
};

// --- hand-written ----------------------------------------------------------------------------
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
  // A period's position by id, and the id at a position: what a hand-coder keeps beside the entities.
  const std::vector<const Ent*> periods = periodsOf(d);
  std::vector<std::uint32_t> position(d.ents[last].size()), id_at(periods.size());
  for (std::size_t i = 0; i < periods.size(); ++i) {
    position[periods[i]->id] = static_cast<std::uint32_t>(i);
    id_at[i]                 = periods[i]->id;
  }

  std::vector<std::uint32_t> ids;  // k a row
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
  // x at row i's ids with the period `back` positions before its own, if that row exists.
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

// --- the usual hand-written code --------------------------------------------------------------
static void runMap(const Data& d, Result& r) {
  using Key           = std::array<const Ent*, kMaxColumns>;  // unused columns stay null
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  const std::vector<const Ent*> periods = periodsOf(d);  // a period's serial is its position
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
  if (impl == "dreamor") {
    switch (d.k) {
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, r); break;
      default: r.skip("k must be 2, 3 or 5");
    }
  } else if (impl == "identity") {
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
