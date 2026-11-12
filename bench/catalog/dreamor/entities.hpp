// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/dreamor/entities.hpp - the dreamor arm of the data family, which ../entities.hpp describes.
#pragma once

#include "../entities.hpp"
#include "arm.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace entities {

template <typename K>
struct cItem : dreamor::EntityColumn<cItem<K>, Item<K>> {
  using dreamor::EntityColumn<cItem<K>, Item<K>>::EntityColumn;
};

template <typename K>
static void runDreamor(const Keys<K>& keys, Result& r) {
  using namespace dreamor::index_set::set_builder;
  using Col = cItem<K>;

  struct Tag {
    double operator()(Col item) const { return tagOf(item->serial); }
  };

  IloEnv env;
  const Solver solver{env};
  dreamor::EntityVector<Item<K>> items;
  r.measure("load", [&] {
    for (std::size_t i = 0; i < keys.present.size(); ++i) {
      items.emplace(keys.present[i], static_cast<std::uint32_t>(i));
    }
    items.freeze();
  });
  r.measure("find_hit", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) {
      sum += items.find(keys.present[i])->serial + 1;
    }
    r.check("find_hit", sum);
  });
  r.measure("find_miss", [&] {
    std::uint64_t found = 0;
    for (const std::uint32_t i : keys.order) {
      found += static_cast<std::uint64_t>(static_cast<bool>(items.find(keys.absent[i])));
    }
    r.check("find_miss", found);
  });
  // A Ref to each entity, by its place in load order. keys.order holds those places.
  std::vector<dreamor::Ref<Item<K>>> by_place;
  for (const Item<K>& item : items) {
    by_place.push_back(items.ref(item));
  }
  // An IndexSet has no empty state. So the set is held in a std::optional, and built inside the phase that times it.
  std::optional<dreamor::IndexSet<Col>> sITEMS;
  std::optional<dreamor::concert::VarTable<Col>> vX;
  r.measure("build", [&] {
    sITEMS.emplace(of<Col>(in<Col>(items)).build());
    vX.emplace(env, *sITEMS, 0, Tag{}, ILOFLOAT);
  });
  using Values = decltype(vX->solution(solver));
  std::optional<Values> plan;
  r.measure("solution", [&] { plan.emplace(vX->solution(solver)); });
  {
    std::uint64_t sum = 0;
    for (const auto& [row, value] : *plan) {
      sum += static_cast<std::uint64_t>(value);
    }
    r.check("solution", sum);
  }
  r.measure("value", [&] {
    std::uint64_t sum = 0;
    for (const std::uint32_t i : keys.order) {
      sum += static_cast<std::uint64_t>((*plan)(Col{by_place[i]}));
    }
    r.check("value", sum);
  });
  env.end();
}

template <typename K>
static void runKey(const std::string& impl, std::size_t n, bool random_order, std::uint64_t seed, Result& r) {
  Keys<K> keys;
  r.setup("keys", [&] { keys = makeKeys<K>(n, random_order, seed); });
  if (impl == "dreamor") {
    runDreamor(keys, r);
  } else {
    r.skip("no arm " + impl + " in ./catalog: the hand-written arms are ./catalog_hand");
  }
}

static void run(const std::string& impl,
                const std::string& key,
                const std::string& access,
                std::size_t n,
                std::uint64_t seed,
                Result& r) {
  if (key == "string") {
    runKey<std::string>(impl, n, access == "random", seed, r);
  } else {
    runKey<std::uint32_t>(impl, n, access == "random", seed, r);
  }
}

}  // namespace entities
