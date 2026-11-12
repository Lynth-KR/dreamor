// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/order.hpp - the dreamor arm of the order family, which ../order.hpp describes.
#pragma once

#include "../order.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace order {

template <typename Seq>
struct Dreamor;

template <std::size_t... J>
struct Dreamor<std::index_sequence<J...>> {
  using G                            = catalog::Gen<std::index_sequence<J...>>;
  static constexpr std::size_t kK    = G::kK;
  static constexpr std::size_t kLast = G::kLast;
  using Set                          = typename G::Set;
  using Table                        = typename G::Table;

  // Returns the column at position I of row. If I is the period's position, returns p instead.
  template <std::size_t I, typename Row>
  static cK<I> replaced(const Row& row, Handle p) {
    if constexpr (I == kLast) {
      return cK<I>{p};
    } else {
      return std::get<cK<I>>(row);
    }
  }

  static void run(const Data& d, Result& r) {
    using namespace dreamor::index_set::set_builder;
    IloEnv env;
    const std::vector<Handle> periods = periodsOf(d);
    const auto sPERIODS               = of<cK<kLast>>(in<cK<kLast>>(periods)).build();
    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<Set> builtX;
    std::optional<Table> vX;
    r.measure("build", [&] {
      builtX.emplace(G::make(d));
      vX.emplace(env, *builtX, 0, typename G::Tag{}, ILOFLOAT);
    });
    const Set& sX = *builtX;
    using Ordered = decltype(sPERIODS.ordered());
    std::optional<Ordered> ordered;
    r.measure("index", [&] {
      // A lookup cannot tell that a key is absent without the index, so this lookup builds it.
      r.sink += vX->contains(replaced<J>(*sX.begin(), d.absent)...);
      ordered.emplace(sPERIODS.ordered());
    });
    // Returns the tag of x at the columns of row with the period p, or 0 if the set has no such row.
    const auto at = [&](const auto& row, Handle p) -> std::uint64_t {
      const auto has_x = [&](auto... c) { return vX->contains(c...); };
      const auto x_at  = [&](auto... c) { return (*vX)(c...); };
      const auto key   = std::tuple{replaced<J>(row, p)...};
      if (!std::apply(has_x, key)) { return 0; }
      return ubOf(std::apply(x_at, key));
    };
    r.measure("step", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        const auto* p = ordered->prev(std::get<cK<kLast>>(row));
        if (p != nullptr) { sum += std::get<0>(*p)->serial + 1; }
      }
      r.check("step", sum);
    });
    r.measure("lag", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        const auto* p = ordered->prev(std::get<cK<kLast>>(row), kLag);
        if (p != nullptr) { sum += at(row, std::get<0>(*p).get()); }
      }
      r.check("lag", sum);
    });
    r.measure("window", [&] {
      std::uint64_t sum = 0;
      for (const auto& row : sX) {
        for (std::ptrdiff_t l = 0; l < kWindow; ++l) {
          const auto* p = ordered->prev(std::get<cK<kLast>>(row), l);
          if (p == nullptr) { break; }
          sum += at(row, std::get<0>(*p).get());
        }
      }
      r.check("window", sum);
    });
    env.end();
  }
};

static void run(const std::string& impl, const Data& d, const Samples&, Result& r) {
  if (d.k == 1) {
    r.skip("no k = 1: the family's operations need a period beside k1");
    return;
  }
  if (impl == "dreamor") {
    switch (d.k) {
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, r); break;
#if CATALOG_MAX_COLUMNS >= 8
      case 8: Dreamor<std::make_index_sequence<8>>::run(d, r); break;
#endif
      default: r.skip("k must be 2, 3 or 5 (8 on ./catalog8)");
    }
  } else {
    r.skip("no arm " + impl + " in ./catalog: the hand-written arms are ./catalog_hand");
  }
}

}  // namespace order
