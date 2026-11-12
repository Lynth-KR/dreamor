// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/setop.hpp - the dreamor arm of the set-operation family, which ../setop.hpp describes.
#pragma once

#include "../setop.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace setop {

// The positions I + Off. Only its return type is used.
template <std::size_t Off, std::size_t... I>
std::index_sequence<(I + Off)...> shifted(std::index_sequence<I...>);

template <typename Seq>
struct Dreamor;

template <std::size_t... J>
struct Dreamor<std::index_sequence<J...>> {
  using G                            = catalog::Gen<std::index_sequence<J...>>;
  static constexpr std::size_t kK    = G::kK;
  static constexpr std::size_t kLast = G::kLast;
  using Set                          = typename G::Set;

  struct InA {
    bool operator()(cK<0> a, cK<kLast> t) const { return inA(a->serial, t->serial); }
  };

  struct InB {
    bool operator()(cK<kLast - 1> b, cK<kLast> t) const { return inB(b->serial, t->serial); }
  };

  // The lists from which B is built: the lists of the data, but with each k1's periods moved one on.
  template <std::size_t I>
  struct ListOfB {
    const Data* d;
    const std::vector<std::vector<Handle>>* moved;

    std::span<const Handle> operator()(cK<0> k1) const {
      if constexpr (I == kLast) {
        return (*moved)[k1->serial];
      } else {
        return d->lists[I][k1->serial];
      }
    }
  };

  template <typename S>
  static std::uint64_t checkRows(const S& set) {
    std::uint64_t h = 0;
    for (const auto& row : set) {
      h += keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<cK<J>>(row)->serial...}, kK);
    }
    return h + set.size() * kCountWeight;
  }

  static void run(const Data& d, Result& r) {
    const auto moved = movedPeriods(d);
    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<Set> builtX;
    r.measure("build", [&] { builtX.emplace(G::make(d)); });
    const Set& sX = *builtX;
    const Set sA  = sX.where(InA{});
    const Set sB  = [&]<std::size_t... I>(std::index_sequence<I...>) {
      using namespace dreamor::index_set::set_builder;
      const auto rows = of<cK<J>...>(in<cK<0>>(d.k1s), in<cK<I + 1>>(ListOfB<I + 1>{&d, &moved})...);
      return d.sparse ? rows.where(typename G::Keep{&d}).where(InB{}).build() : rows.where(InB{}).build();
    }(std::make_index_sequence<kK - 1>{});
    select(decltype(shifted<1>(std::make_index_sequence<kK - 1>{})){}, sX, r);
    {
      std::optional<Set> out;
      r.measure("unite", [&] { out.emplace(dreamor::unite(sA, sB)); });
      r.check("unite", checkRows(*out));
    }
    {
      std::optional<Set> out;
      r.measure("intersect", [&] { out.emplace(dreamor::intersect(sA, sB)); });
      r.check("intersect", checkRows(*out));
    }
    {
      std::optional<Set> out;
      r.measure("difference", [&] { out.emplace(dreamor::difference(sA, sB)); });
      r.check("difference", checkRows(*out));
    }
    {
      std::optional<Set> out;
      r.measure("member", [&] {
        const auto in_b = sB.membership();
        out.emplace(sX.where(in_b));
      });
      r.check("member", checkRows(*out));
    }
  }

  template <std::size_t... I>
  static void select(std::index_sequence<I...>, const Set& sX, Result& r) {
    // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
    std::optional<dreamor::IndexSet<cK<I>...>> out;
    r.measure("select", [&] { out.emplace(sX.template select<cK<I>...>()); });
    std::uint64_t h = 0;
    for (const auto& row : *out) {
      h += keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<cK<I>>(row)->serial...}, sizeof...(I));
    }
    r.check("select", h + out->size() * kCountWeight);
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

}  // namespace setop
