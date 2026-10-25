// SPDX-License-Identifier: Apache-2.0
// catalog/group.hpp - the group family: a set grouped by some of its columns, a sum per group
//
// Three shapes, by the column a grouping leaves out:
//   inner  the period: every group is a run of the generation order
//   lead   the first column, k1
//   mid    the second column (k >= 3 only)
// Phases, the same in every arm: build (the rows and x on them), then for each shape prep_<shape>
// (the grouping) and sum_<shape> (one expression a group, x over its rows), and sum_where (one
// expression an inner group, x over its rows in even periods only).
//
// dreamor's groupBy and sumOf are set against hand-written groupings over packed ids: through a
// std::unordered_map from the group's key to its number, with the identity hash (`identity`) or
// a mixed one (`mixed`), counting the rows into place (CSR); by sorting (key, row) pairs (`sort`);
// and the usual code, a std::map from the key's entities to its rows (`map`). The packed arms use
// runs for the inner shape, as a hand-coder who knows the generation order would; `map` does not.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "shared.hpp"

namespace group {

using catalog::consume;
using catalog::keyOf;
using catalog::MixedHash;
using catalog::Packer;

// The columns a shape keeps, for k columns.
inline std::vector<std::size_t> keptBy(const std::string& shape, std::size_t k) {
  std::vector<std::size_t> kept;
  for (std::size_t j = 0; j < k; ++j) {
    const bool dropped = (shape == "inner" && j == k - 1) || (shape == "lead" && j == 0) || (shape == "mid" && j == 1);
    if (!dropped) kept.push_back(j);
  }
  return kept;
}
inline std::vector<std::string> shapesFor(std::size_t k) {
  return k >= 3 ? std::vector<std::string>{"inner", "lead", "mid"} : std::vector<std::string>{"inner", "lead"};
}
inline bool evenPeriod(const Ent* t) {
  return t->serial % 2 == 0;
}

// --- dreamor ---------------------------------------------------------------------------------
template <std::size_t Off, std::size_t... I>
std::index_sequence<(I + Off)...> shifted(std::index_sequence<I...>);
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
    bool operator()(KCol<kLast> t) const { return evenPeriod(t.get()); }
  };

  static void run(const Data& d, Result& r) {
    IloEnv env;
    Set sX;
    std::optional<Table> vX;
    r.measure("build", [&] {
      sX = G::make(d);
      vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT);
    });
    shape("inner", std::make_index_sequence<kK - 1>{}, sX, *vX, env, r, true);
    shape("lead", decltype(shifted<1>(std::make_index_sequence<kK - 1>{})){}, sX, *vX, env, r, false);
    if constexpr (kK >= 3)
      shape("mid", decltype(skipSecond(std::make_index_sequence<kK - 2>{})){}, sX, *vX, env, r, false);
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
    using Grouped = decltype(sX.template groupBy<KCol<I>...>());
    std::optional<Grouped> groups;
    r.measure("prep_" + name, [&] { groups.emplace(sX.template groupBy<KCol<I>...>()); });
    const auto keyOfGroup = [](const auto& group) {
      return keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<KCol<I>>(group.key)->serial...}, sizeof...(I));
    };
    r.measure("sum_" + name, [&] {
      std::uint64_t check = 0;
      for (const auto& group : *groups) {
        IloExpr e = dreamor::concert::sumOf(env, group.tuples, vX).build();
        check += consume(e, keyOfGroup(group));
      }
      r.check("sum_" + name, check);
    });
    if (with_where) {
      r.measure("sum_where", [&] {
        std::uint64_t check = 0;
        for (const auto& group : *groups) {
          IloExpr e = dreamor::concert::sumOf(env, group.tuples, vX).where(Even{}).build();
          check += consume(e, keyOfGroup(group));
        }
        r.check("sum_where", check);
      });
    }
  }
};

// --- hand-written ----------------------------------------------------------------------------
enum class Way { kIdentity, kMixed, kSort, kMap };

// Groups as a hand-written arm keeps them: rows in group order and where each group starts.
struct Groups {
  std::vector<std::uint32_t> rows, start;  // start: one past the end of the last group at the back
};

static void runHand(Way way, const Data& d, Result& r) {
  const std::size_t k = d.k, last = d.last();
  IloEnv env;
  Packer full;
  full.k = k;
  for (std::size_t j = 0; j < k; ++j) full.size[j] = d.ents[j].size();
  if (way != Way::kMap && full.span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    r.skip("the ids do not pack into 64 bits");
    env.end();
    return;
  }
  std::vector<std::uint32_t> ids;  // k a row
  std::vector<const Ent*> ents;    // k a row, for the map arm
  std::vector<IloNumVar> x;
  std::array<std::vector<std::uint32_t>, kMaxColumns> serial_of;  // by id
  for (std::size_t j = 0; j < k; ++j) {
    serial_of[j].resize(d.ents[j].size());
    for (const auto& e : d.ents[j]) serial_of[j][e->id] = e->serial;
  }
  r.measure("build", [&] {
    ids.reserve(d.rows * k);
    if (way == Way::kMap) ents.reserve(d.rows * k);
    x.reserve(d.rows);
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      std::array<std::uint32_t, kMaxColumns> serial{};
      for (std::size_t j = 0; j < k; ++j) {
        ids.push_back(row[j]->id);
        serial[j] = row[j]->serial;
        if (way == Way::kMap) ents.push_back(row[j]);
      }
      x.emplace_back(env, 0, tagOf(serial, k, 0), ILOFLOAT);
    });
  });
  const auto groupKey = [&](std::size_t row, const std::vector<std::size_t>& kept) {
    std::array<std::uint32_t, kMaxColumns> s{};
    for (std::size_t i = 0; i < kept.size(); ++i) s[i] = serial_of[kept[i]][ids[row * k + kept[i]]];
    return keyOf(s, kept.size());
  };

  for (const std::string& shape : shapesFor(k)) {
    const std::vector<std::size_t> kept = keptBy(shape, k);
    Packer pack;
    pack.k = kept.size();
    for (std::size_t i = 0; i < kept.size(); ++i) pack.size[i] = d.ents[kept[i]].size();
    const auto packed = [&](std::size_t row) {
      std::array<std::uint32_t, kMaxColumns> id{};
      for (std::size_t i = 0; i < kept.size(); ++i) id[i] = ids[row * k + kept[i]];
      return pack(id);
    };
    Groups g;
    r.measure("prep_" + shape, [&] {
      const std::size_t n = x.size();
      if (shape == "inner" && way != Way::kMap) {
        // The period is innermost, so a group's rows come one after another.
        for (std::size_t i = 0; i < n; ++i) {
          if (i == 0 || packed(i) != packed(i - 1)) {
            g.start.push_back(static_cast<std::uint32_t>(i));
          }
        }
        g.rows.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) g.rows[i] = i;
      } else if (way == Way::kIdentity || way == Way::kMixed) {
        const auto csr = [&](auto& index) {
          std::vector<std::uint32_t> group_of(n), count;
          index.reserve(n);
          for (std::size_t i = 0; i < n; ++i) {
            const auto [it, fresh] = index.try_emplace(packed(i), static_cast<std::uint32_t>(count.size()));
            if (fresh) count.push_back(0);
            group_of[i] = it->second;
            ++count[it->second];
          }
          g.start.resize(count.size());
          std::uint32_t running = 0;
          for (std::size_t c = 0; c < count.size(); ++c) g.start[c] = std::exchange(running, running + count[c]);
          g.rows.resize(n);
          std::vector<std::uint32_t> next(g.start);
          for (std::uint32_t i = 0; i < n; ++i) g.rows[next[group_of[i]]++] = i;
        };
        if (way == Way::kIdentity) {
          std::unordered_map<std::uint64_t, std::uint32_t> index;
          csr(index);
        } else {
          std::unordered_map<std::uint64_t, std::uint32_t, MixedHash> index;
          csr(index);
        }
      } else if (way == Way::kSort) {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> order(n);
        for (std::uint32_t i = 0; i < n; ++i) order[i] = {packed(i), i};
        std::sort(order.begin(), order.end());  // by key, then by row: rows keep their order
        g.rows.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
          g.rows[i] = order[i].second;
          if (i == 0 || order[i].first != order[i - 1].first) {
            g.start.push_back(static_cast<std::uint32_t>(i));
          }
        }
      } else {
        std::map<std::array<const Ent*, kMaxColumns>, std::vector<std::uint32_t>> groups;
        for (std::uint32_t i = 0; i < n; ++i) {
          std::array<const Ent*, kMaxColumns> key{};
          for (std::size_t c = 0; c < kept.size(); ++c) key[c] = ents[i * k + kept[c]];
          groups[key].push_back(i);
        }
        for (const auto& [key, rows] : groups) {
          g.start.push_back(static_cast<std::uint32_t>(g.rows.size()));
          g.rows.insert(g.rows.end(), rows.begin(), rows.end());
        }
      }
      g.start.push_back(static_cast<std::uint32_t>(n));
    });
    const auto sum = [&](const std::string& phase, bool even_only) {
      r.measure(phase, [&] {
        std::uint64_t check = 0;
        for (std::size_t c = 0; c + 1 < g.start.size(); ++c) {
          IloExpr e(env);
          for (std::uint32_t p = g.start[c]; p < g.start[c + 1]; ++p) {
            const std::uint32_t i = g.rows[p];
            if (even_only && serial_of[last][ids[i * k + last]] % 2 != 0) continue;
            e += x[i];
          }
          // The group's key, from its first row, as dreamor's arm reads it from the group.
          check += consume(e, groupKey(g.rows[g.start[c]], kept));
        }
        r.check(phase, check);
      });
    };
    sum("sum_" + shape, false);
    if (shape == "inner") sum("sum_where", true);
  }
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
    runHand(Way::kIdentity, d, r);
  } else if (impl == "mixed") {
    runHand(Way::kMixed, d, r);
  } else if (impl == "sort") {
    runHand(Way::kSort, d, r);
  } else if (impl == "map") {
    runHand(Way::kMap, d, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace group
