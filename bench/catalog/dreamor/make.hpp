// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/make.hpp - the dreamor arm of the make family, which ../make.hpp describes.
#pragma once

#include "../make.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace make {

template <typename Seq>
struct Dreamor;

template <std::size_t... J>
struct Dreamor<std::index_sequence<J...>> {
  using G                            = catalog::Gen<std::index_sequence<J...>>;
  static constexpr std::size_t kK    = G::kK;
  static constexpr std::size_t kLast = G::kLast;
  using Set                          = typename G::Set;
  using Table                        = typename G::Table;

  template <typename S>
  static std::uint64_t check(const S& set) {
    std::uint64_t sum = 0;
    for (const auto& row : set) {
      sum += rowCheck(std::array<std::uint32_t, kMaxColumns>{std::get<cK<J>>(row)->serial...}, kK);
    }
    return sum;
  }

  static void run(const Data& d, Result& r) {
    if constexpr (kK == 1) {
      runSingle(d, r);
    } else {
      runJoined(d, r, std::make_index_sequence<kK - 2>{});
    }
  }

  // At k = 1 a row has no period to join on, so every phase runs but the join.
  static void runSingle(const Data& d, Result& r) {
    IloEnv env;
    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<Set> builtX;
    r.measure("collect", [&] { builtX.emplace(G::make(d)); });
    const Set& sX = *builtX;
    r.check("collect", check(sX));
    std::optional<Table> vX, vNamed;
    r.measure("vars", [&] { vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT); });
    if (vX) {
      std::uint64_t sum = 0;
      for (const auto& [row, x] : *vX) {
        sum += ubOf(x);
      }
      r.check("vars", sum);
    }
    r.measure("vars_named", [&] { vNamed.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT, "x"); });
    if (vNamed) {
      std::uint64_t sum = 0;
      for (const auto& [row, x] : *vNamed) {
        sum += ubOf(x);
      }
      r.check("vars_named", sum);
    }
    env.end();
  }

  // M + 1 is the position of each middle column.
  template <std::size_t... M>
  static void runJoined(const Data& d, Result& r, std::index_sequence<M...>)
    requires(kK > 1)
  {
    using namespace dreamor::index_set::set_builder;
    IloEnv env;
    // A holds k1 and the middle columns, and B holds k1 and the period.
    const auto sA =
        of<cK<0>, cK<M + 1>...>(in<cK<0>>(d.k1s), in<cK<M + 1>>(typename G::template ListOf<M + 1>{&d})...).build();
    const auto sB =
        of<cK<0>, cK<kLast>>(in<cK<0>>(d.k1s), in<cK<kLast>>(typename G::template ListOf<kLast>{&d})).build();

    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<Set> builtX;
    r.measure("collect", [&] { builtX.emplace(G::make(d)); });
    const Set& sX = *builtX;
    r.check("collect", check(sX));
    std::optional<Table> vX, vNamed;
    r.measure("vars", [&] { vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT); });
    if (vX) {
      std::uint64_t sum = 0;
      for (const auto& [row, x] : *vX) {
        sum += ubOf(x);
      }
      r.check("vars", sum);
    }
    r.measure("vars_named", [&] { vNamed.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT, "x"); });
    if (vNamed) {
      std::uint64_t sum = 0;
      for (const auto& [row, x] : *vNamed) {
        sum += ubOf(x);
      }
      r.check("vars_named", sum);
    }
    std::optional<Set> joined;
    r.measure("join", [&] {
      // The generator over B shares k1 with A, so it joins B to A on k1.
      auto rows = of<cK<J>...>(in<cK<0>, cK<M + 1>...>(sA), in<cK<0>, cK<kLast>>(sB));
      joined.emplace(d.sparse ? std::move(rows).where(typename G::Keep{&d}).build() : std::move(rows).build());
    });
    if (joined) { r.check("join", check(*joined)); }
    env.end();
  }
};

static void run(const std::string& impl, const Data& d, const Samples&, Result& r) {
  if (impl == "dreamor") {
    switch (d.k) {
      case 1: Dreamor<std::make_index_sequence<1>>::run(d, r); break;
      case 2: Dreamor<std::make_index_sequence<2>>::run(d, r); break;
      case 3: Dreamor<std::make_index_sequence<3>>::run(d, r); break;
      case 5: Dreamor<std::make_index_sequence<5>>::run(d, r); break;
#if CATALOG_MAX_COLUMNS >= 8
      case 8: Dreamor<std::make_index_sequence<8>>::run(d, r); break;
#endif
      default: r.skip("k must be 1, 2, 3 or 5 (8 on ./catalog8)");
    }
  } else {
    r.skip("no arm " + impl + " in ./catalog: the hand-written arms are ./catalog_hand");
  }
}

}  // namespace make
