// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/slice.hpp - the dreamor arm of the slice family, which ../slice.hpp describes.
#pragma once

#include "../slice.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "group.hpp"

namespace slice {

using Key     = KeyOf<DreamorArm>;
using Driver  = DriverOf<DreamorArm>;
using Drivers = DriversOf<DreamorArm>;

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
    bool operator()(cK<kLast> t) const { return group::evenPeriod(t.get()); }
  };

  struct Odd {
    bool operator()(cK<kLast> t) const { return !group::evenPeriod(t.get()); }
  };

  // The term of a row, c(t) * x, as a model writes cost[t] * x[...]. The term looks x up by the columns of the row.
  struct Coef {
    const Table* x;

    auto operator()(cK<J>... c) const {
      const std::array<Handle, kK> e{c.get()...};
      return coefOf(e[kLast]->serial) * (*x)(c...);
    }
  };

  static void run(const Data& d, const Drivers& drivers, Result& r) {
    IloEnv env;
    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<Set> builtX;
    std::optional<Table> vX;
    r.measure("build", [&] {
      builtX.emplace(G::make(d));
      vX.emplace(env, *builtX, 0, typename G::Tag{}, ILOFLOAT);
    });
    const Set& sX = *builtX;
    shape(drivers[0], std::make_index_sequence<kK - 1>{}, sX, *vX, env, r);
    shape(drivers[1], decltype(group::shifted<1>(std::make_index_sequence<kK - 1>{})){}, sX, *vX, env, r);
    env.end();
  }

  template <std::size_t... I>
  static void shape(
      const Driver& dr, std::index_sequence<I...>, const Set& sX, const Table& vX, IloEnv env, Result& r) {
    using namespace dreamor::index_set::set_builder;
    using Row = std::tuple<cK<I>...>;
    std::vector<Row> rows;
    rows.reserve(dr.keys.size());
    [&]<std::size_t... P>(std::index_sequence<P...>) {
      for (const Key& key : dr.keys) {
        rows.emplace_back(cK<I>{key[P]}...);
      }
    }(std::make_index_sequence<sizeof...(I)>{});
    const auto sDRIVER = of<cK<I>...>(in<cK<I>...>(rows)).build();
    const auto check   = [](const Row& key) {
      return keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<cK<I>>(key)->serial...}, sizeof...(I));
    };

    using Grouped = decltype(sX.template groupBy<cK<I>...>());
    std::optional<Grouped> groups;
    r.measure("prep_" + dr.shape, [&] { groups.emplace(sX.template groupBy<cK<I>...>()); });
    r.measure("slice_" + dr.shape, [&] {
      std::uint64_t sum = 0;
      for (const auto& key : sDRIVER) {
        IloExpr e = dreamor::concert::sumExpr(env, groups->slice(key), vX).build();
        sum += consume(e, check(key));
      }
      r.check("slice_" + dr.shape, sum);
    });
    if (dr.shape != "inner") { return; }
    r.measure("split", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : sDRIVER) {
        auto parts            = dreamor::concert::sumExpr(env, groups->slice(key), vX).whereEach(Even{}, Odd{}).build();
        const std::uint64_t h = check(key);
        sum += consume(parts[0], h) + consume(parts[1], ~h);
      }
      r.check("split", sum);
    });
    r.measure("coef", [&] {
      std::uint64_t sum = 0;
      for (const auto& key : sDRIVER) {
        IloExpr e = dreamor::concert::sumExpr(env, groups->slice(key), Coef{&vX}).build();
        sum += consume(e, check(key));
      }
      r.check("coef", sum);
    });
  }
};

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
  if (impl == "dreamor") {
    switch (d.k) {
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, drivers, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, drivers, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, drivers, r); break;
#if CATALOG_MAX_COLUMNS >= 8
      case 8: Dreamor<std::make_index_sequence<8>>::run(d, drivers, r); break;
#endif
      default: r.skip("k must be 2, 3 or 5 (8 on ./catalog8)");
    }
  } else {
    r.skip("no arm " + impl + " in ./catalog: the hand-written arms are ./catalog_hand");
  }
}

}  // namespace slice
