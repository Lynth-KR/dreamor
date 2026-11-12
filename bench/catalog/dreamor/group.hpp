// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/group.hpp - the dreamor arm of the group family, which ../group.hpp describes.
#pragma once

#include "../group.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace group {

inline bool evenPeriod(Handle t) {
  return t->serial % 2 == 0;
}

// The positions I + Off. Only its return type is used.
template <std::size_t Off, std::size_t... I>
std::index_sequence<(I + Off)...> shifted(std::index_sequence<I...>);

// Position 0, then the positions I + 2: every position but 1. Only its return type is used.
template <std::size_t... I>
std::index_sequence<0, (I + 2)...> skipSecond(std::index_sequence<I...>);

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
    bool operator()(cK<kLast> t) const { return evenPeriod(t.get()); }
  };

  static void run(const Data& d, Result& r) {
    IloEnv env;
    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<Set> builtX;
    std::optional<Table> vX;
    r.measure("build", [&] {
      builtX.emplace(G::make(d));
      vX.emplace(env, *builtX, 0, typename G::Tag{}, ILOFLOAT);
    });
    const Set& sX = *builtX;
    shape("inner", std::make_index_sequence<kK - 1>{}, sX, *vX, env, r, true);
    shape("lead", decltype(shifted<1>(std::make_index_sequence<kK - 1>{})){}, sX, *vX, env, r, false);
    if constexpr (kK >= 3) {
      shape("mid", decltype(skipSecond(std::make_index_sequence<kK - 2>{})){}, sX, *vX, env, r, false);
    }
    env.end();
  }

  template <std::size_t... I>
  static void shape(const std::string& name,
                    std::index_sequence<I...>,
                    const Set& sX,
                    const Table& vX,
                    IloEnv env,
                    Result& r,
                    bool with_where) {
    using Grouped = decltype(sX.template groupBy<cK<I>...>());
    std::optional<Grouped> groups;
    r.measure("prep_" + name, [&] { groups.emplace(sX.template groupBy<cK<I>...>()); });
    const auto keyOfGroup = [](const auto& group) {
      return keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<cK<I>>(group.key)->serial...}, sizeof...(I));
    };
    r.measure("sum_" + name, [&] {
      std::uint64_t check = 0;
      for (const auto& group : *groups) {
        IloExpr e = dreamor::concert::sumExpr(env, group.rows, vX).build();
        check += consume(e, keyOfGroup(group));
      }
      r.check("sum_" + name, check);
    });
    if (with_where) {
      r.measure("sum_where", [&] {
        std::uint64_t check = 0;
        for (const auto& group : *groups) {
          IloExpr e = dreamor::concert::sumExpr(env, group.rows, vX).where(Even{}).build();
          check += consume(e, keyOfGroup(group));
        }
        r.check("sum_where", check);
      });
    }
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

}  // namespace group
