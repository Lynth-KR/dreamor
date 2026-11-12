// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/lookup.hpp - the dreamor arm of the lookup family, which ../lookup.hpp describes.
#pragma once

#include "../lookup.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace lookup {

template <typename Seq>
struct Dreamor;

template <std::size_t... J>
struct Dreamor<std::index_sequence<J...>> {
  using G                            = catalog::Gen<std::index_sequence<J...>>;
  static constexpr std::size_t kK    = G::kK;
  static constexpr std::size_t kLast = G::kLast;
  using Set                          = typename G::Set;
  using Table                        = typename G::Table;
  // Whether the cell has y, on the (k1, period) pairs. A cell at k = 1 has no period, so it has no y.
  static constexpr bool kPairs = kK > 1;
  using PairSet                = std::conditional_t<kPairs, dreamor::IndexSet<cK<0>, cK<kLast>>, std::monostate>;
  using PairTable = std::conditional_t<kPairs, dreamor::concert::VarTable<cK<0>, cK<kLast>>, std::monostate>;

  struct PairTag {
    double operator()(cK<0> a, cK<kLast> t) const {
      return tagOf(std::array<std::uint32_t, 2>{a->serial, t->serial}, 2, 1);
    }
  };

  static void run(const Data& d, const Samples& s, Result& r) {
    IloEnv env;
    // An IndexSet has no empty state. So the sets are held in std::optionals, and built inside the phase that times
    // them.
    std::optional<Set> builtX;
    std::optional<PairSet> builtY;
    std::optional<Table> vX;
    std::optional<PairTable> vY;
    r.measure("build", [&] {
      builtX.emplace(G::make(d));
      vX.emplace(env, *builtX, 0, typename G::Tag{}, ILOFLOAT);
      if constexpr (kPairs) {
        builtY.emplace(builtX->template select<cK<0>, cK<kLast>>());
        vY.emplace(env, *builtY, 0, PairTag{}, ILOFLOAT);
      }
    });
    const Set& sX = *builtX;
    r.measure("index", [&] {
      // A lookup cannot tell that a key is absent without the index, so these lookups build the indexes of x and y.
      const auto& row = *sX.begin();
      r.sink += vX->contains(replaced<J>(row, d.absent)...);
      if constexpr (kPairs) { r.sink += vY->contains(std::get<cK<0>>(row), cK<kLast>{d.absent}); }
    });
    if constexpr (kPairs) {
      r.measure("neighbor", [&] {
        const auto has_x  = [&](auto... c) { return vX->contains(c...); };
        const auto x_at   = [&](auto... c) { return (*vX)(c...); };
        std::uint64_t sum = 0;
        for (const auto& row : sX) {
          Handle p = d.prev(std::get<cK<kLast>>(row).get());
          if (!p) { continue; }
          const auto key = std::tuple{replaced<J>(row, p)...};
          if (std::apply(has_x, key)) { sum += ubOf(std::apply(x_at, key)); }
        }
        r.check("neighbor", sum);
      });
      r.measure("project", [&] {
        std::uint64_t sum = 0;
        for (const auto& row : sX) {
          sum += ubOf((*vY)(std::get<cK<0>>(row), std::get<cK<kLast>>(row)));
        }
        r.check("project", sum);
      });
    }
    r.measure("hit", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : s.hit) {
        sum += ubOf((*vX)(cK<J>{key[J]}...));
      }
      r.check("hit", sum);
    });
    r.measure("miss", [&] {
      std::uint64_t found = 0;
      for (const auto& key : s.miss) {
        found += vX->contains(cK<J>{key[J]}...);
      }
      r.check("miss", found);
    });
    r.measure("half", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : s.half) {
        if (vX->contains(cK<J>{key[J]}...)) { sum += ubOf((*vX)(cK<J>{key[J]}...)); }
      }
      r.check("half", sum);
    });
    env.end();
  }

  // Returns the column at position I of row. If I is the period's position, returns p instead.
  template <std::size_t I, typename Row>
  static cK<I> replaced(const Row& row, Handle p) {
    if constexpr (I == kLast) {
      return cK<I>{p};
    } else {
      return std::get<cK<I>>(row);
    }
  }
};

static void run(const std::string& impl, const Data& d, const Samples& s, Result& r) {
  if (impl == "dreamor") {
    switch (d.k) {
      case 1: Dreamor<std::make_index_sequence<1>>::run(d, s, r); break;
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, s, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, s, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, s, r); break;
#if CATALOG_MAX_COLUMNS >= 8
      case 8: Dreamor<std::make_index_sequence<8>>::run(d, s, r); break;
#endif
      default: r.skip("k must be 1, 2, 3 or 5 (8 on ./catalog8)");
    }
  } else {
    r.skip("no arm " + impl + " in ./catalog: the hand-written arms are ./catalog_hand");
  }
}

}  // namespace lookup
