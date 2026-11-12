// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/arm.hpp - the dreamor arm's trait, its columns, and the set that every family of the arm builds.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <utility>

#include "../data.hpp"
#include "../shared.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

// The trait that data.hpp describes. A row holds a Ref into the EntityVector of its column.
struct DreamorArm {
  using Ents   = dreamor::EntityVector<Ent>;
  using Handle = dreamor::Ref<Ent>;

  static void add(Ents& ents, const Ent& e) { static_cast<void>(ents.insert(e)); }

  static void freeze(Ents& ents) { ents.freeze(); }

  static Handle handle(const Ents& ents, const Ent& e) { return ents.at(e.id); }
};

using Ents    = DreamorArm::Ents;
using Handle  = DreamorArm::Handle;
using Data    = DataOf<DreamorArm>;
using Samples = SamplesOf<DreamorArm>;

// The column at position J of a row. Position 0 is k1, and the last position is the period. The positions between
// them are the middle columns. Every column holds the entity type Ent, so each position needs a column type of its
// own.
template <std::size_t J>
struct cK : dreamor::EntityColumn<cK<J>, Ent> {
  using dreamor::EntityColumn<cK<J>, Ent>::EntityColumn;
};

namespace catalog {

// The set that every family of the dreamor arm builds, over the k columns cK<J>..., and variable x on it.
template <typename Seq>
struct Gen;

template <std::size_t... J>
struct Gen<std::index_sequence<J...>> {
  static constexpr std::size_t kK    = sizeof...(J);
  static constexpr std::size_t kLast = kK - 1;
  using Set                          = dreamor::IndexSet<cK<J>...>;
  using Table                        = dreamor::concert::VarTable<cK<J>...>;

  // dreamor finds the columns that a callable reads from the types of its parameters. So the callables below are
  // function objects with a parameter for each column that they read, not generic lambdas.

  // The condition of a sparse cell, which DataOf::keep decides.
  struct Keep {
    const Data* d;

    bool operator()(cK<J>... c) const { return d->keep(std::array<std::uint32_t, kMaxColumns>{c->serial...}); }
  };

  // The upper bound of a row's variable x: the tag of the row's serials.
  struct Tag {
    double operator()(cK<J>... c) const { return tagOf(std::array<std::uint32_t, kMaxColumns>{c->serial...}, kK, 0); }
  };

  // The list of column I that a k1 offers.
  template <std::size_t I>
  struct ListOf {
    const Data* d;

    std::span<const Handle> operator()(cK<0> k1) const { return d->lists[I][k1->serial]; }
  };

  // Returns the set: each k1, with each entity of each list that the k1 offers. If the cell is sparse, the set holds
  // only the rows that Keep keeps.
  static Set make(const Data& d) {
    using namespace dreamor::index_set::set_builder;
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      const auto rows = of<cK<J>...>(in<cK<0>>(d.k1s), in<cK<I + 1>>(ListOf<I + 1>{&d})...);
      return d.sparse ? rows.where(Keep{&d}).build() : rows.build();
    }(std::make_index_sequence<kK - 1>{});
  }
};

}  // namespace catalog
