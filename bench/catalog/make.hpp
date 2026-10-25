// SPDX-License-Identifier: Apache-2.0
// catalog/make.hpp - the make family: a set generated, variables made on it, a set joined
//
// What the other families measure together as build, apart:
//   collect     the rows: k1, then column j's list of each k1, kept by the predicate when the cell
//               is sparse
//   vars        a variable a row, its upper bound the row's tag, unnamed
//   vars_named  the same, each named x_<id>_<id>..., as a model names its variables
//   join        the same rows again, as A (every column but the period) joined with B (k1 and
//               the period) on k1: the rows collect makes, so the two phases' checks agree
// A and B are made before any phase and not measured.
//
// dreamor's makeSet and VarTable are set against nested loops into a std::vector, a variable a
// row, and names written with std::to_chars - the same code in every hand-written arm - and joins
// as a hand-coder writes them: B's periods grouped by k1 through a std::unordered_map from k1's id
// (`identity`, `mixed`); B sorted by k1's id and searched with std::equal_range (`sort`); and the
// usual code, a std::map from k1 to its periods (`map`).
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
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

namespace make {

using catalog::keyOf;
using catalog::MixedHash;
using catalog::ubOf;

// A set's rows, folded so that the order they come in does not count: the check of collect and join.
template <typename Serials>
inline std::uint64_t rowCheck(const Serials& serials, std::size_t k) {
  return keyOf(serials, k) | 1;
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

  template <typename S>
  static std::uint64_t check(const S& set) {
    std::uint64_t sum = 0;
    for (const auto& row : set) {
      sum += rowCheck(std::array<std::uint32_t, kMaxColumns>{std::get<KCol<J>>(row)->serial...}, kK);
    }
    return sum;
  }

  static void run(const Data& d, Result& r) {
    IloEnv env;
    // A: k1 and the middle columns; B: k1 and the period.
    const auto sA = [&]<std::size_t... M>(std::index_sequence<M...>) {
      return dreamor::makeSet<KCol<0>, KCol<M + 1>...>(d.k1s, typename G::template ListOf<M + 1>{&d}...).collect();
    }(std::make_index_sequence<kK - 2>{});
    const auto sB = dreamor::makeSet<KCol<0>, KCol<kLast>>(d.k1s, typename G::template ListOf<kLast>{&d}).collect();

    Set sX;
    r.measure("collect", [&] { sX = G::make(d); });
    r.check("collect", check(sX));
    std::optional<Table> vX, vNamed;
    r.measure("vars", [&] { vX.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT); });
    if (vX) {
      std::uint64_t sum = 0;
      for (const auto& [row, x] : *vX) sum += ubOf(x);
      r.check("vars", sum);
    }
    r.measure("vars_named", [&] { vNamed.emplace(env, sX, 0, typename G::Tag{}, ILOFLOAT, "x"); });
    if (vNamed) {
      std::uint64_t sum = 0;
      for (const auto& [row, x] : *vNamed) sum += ubOf(x);
      r.check("vars_named", sum);
    }
    std::optional<Set> sJoined;
    r.measure("join", [&] {
      sJoined.emplace(d.sparse ? dreamor::makeSet<>(sA, sB).where(typename G::Keep{&d}).collect()
                               : dreamor::makeSet<>(sA, sB).collect());
    });
    if (sJoined) r.check("join", check(*sJoined));
    env.end();
  }
};

// --- hand-written ----------------------------------------------------------------------------
enum class Way { kIdentity, kMixed, kSort, kMap };

static void runHand(Way way, const Data& d, Result& r) {
  const std::size_t k = d.k, last = d.last();
  // A's rows, k - 1 entities each, and B's (k1, period) pairs, in generation order.
  std::vector<const Ent*> a_rows;
  std::vector<std::pair<const Ent*, const Ent*>> b_rows;
  {
    std::array<const Ent*, kMaxColumns> row{};
    const auto walk = [&](auto& self, std::size_t j) -> void {
      if (j == last) {
        a_rows.insert(a_rows.end(), row.begin(), row.begin() + static_cast<std::ptrdiff_t>(last));
        return;
      }
      for (const Ent* e : d.lists[j][row[0]->serial]) {
        row[j] = e;
        self(self, j + 1);
      }
    };
    for (const Ent* a : d.k1s) {
      row[0] = a;
      walk(walk, 1);
      for (const Ent* t : d.lists[last][a->serial]) b_rows.emplace_back(a, t);
    }
  }

  IloEnv env;
  std::vector<const Ent*> rows;  // k a row
  r.measure("collect", [&] {
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      rows.insert(rows.end(), row.begin(), row.begin() + static_cast<std::ptrdiff_t>(k));
    });
  });
  const std::size_t n  = rows.size() / k;
  const auto serialsAt = [&](const std::vector<const Ent*>& from, std::size_t i) {
    std::array<std::uint32_t, kMaxColumns> s{};
    for (std::size_t j = 0; j < k; ++j) s[j] = from[i * k + j]->serial;
    return s;
  };
  const auto setCheck = [&](const std::vector<const Ent*>& from) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < from.size() / k; ++i) sum += rowCheck(serialsAt(from, i), k);
    return sum;
  };
  r.check("collect", setCheck(rows));

  std::vector<IloNumVar> x, named;
  r.measure("vars", [&] {
    x.reserve(n);
    for (std::size_t i = 0; i < n; ++i) x.emplace_back(env, 0, tagOf(serialsAt(rows, i), k, 0), ILOFLOAT);
  });
  {
    std::uint64_t sum = 0;
    for (const IloNumVar& v : x) sum += ubOf(v);
    r.check("vars", sum);
  }
  r.measure("vars_named", [&] {
    named.reserve(n);
    std::array<char, 256> name{};
    for (std::size_t i = 0; i < n; ++i) {
      char* at = name.data();
      *at++    = 'x';
      for (std::size_t j = 0; j < k; ++j) {
        *at++ = '_';
        at    = std::to_chars(at, name.data() + name.size() - 1, keyName(rows[i * k + j])).ptr;
      }
      *at = '\0';
      named.emplace_back(env, 0, tagOf(serialsAt(rows, i), k, 0), ILOFLOAT, name.data());
    }
  });
  {
    std::uint64_t sum = 0;
    for (const IloNumVar& v : named) sum += ubOf(v);
    r.check("vars_named", sum);
  }

  std::vector<const Ent*> joined;  // k a row
  r.measure("join", [&] {
    const std::size_t a_width = last;
    std::array<std::uint32_t, kMaxColumns> serials{};
    std::array<const Ent*, kMaxColumns> row{};
    // Emits A's row i with each of the periods first..last-1, kept by the predicate.
    const auto emit = [&](std::size_t i, auto first, auto end, auto period_of) {
      for (std::size_t j = 0; j < a_width; ++j) {
        row[j]     = a_rows[i * a_width + j];
        serials[j] = row[j]->serial;
      }
      for (auto it = first; it != end; ++it) {
        row[last]     = period_of(*it);
        serials[last] = row[last]->serial;
        if (d.keep(serials)) joined.insert(joined.end(), row.begin(), row.begin() + static_cast<std::ptrdiff_t>(k));
      }
    };
    const std::size_t a_count = a_rows.size() / a_width;
    if (way == Way::kIdentity || way == Way::kMixed) {
      const auto hashed = [&](auto& group_of) {
        // B's periods grouped by k1 (CSR), the group found through k1's id.
        std::vector<std::uint32_t> start, count;
        std::vector<const Ent*> periods(b_rows.size());
        std::vector<std::uint32_t> group(b_rows.size());
        group_of.reserve(b_rows.size());
        for (std::size_t i = 0; i < b_rows.size(); ++i) {
          const auto [it, fresh] = group_of.try_emplace(b_rows[i].first->id, static_cast<std::uint32_t>(count.size()));
          if (fresh) count.push_back(0);
          group[i] = it->second;
          ++count[it->second];
        }
        start.resize(count.size() + 1);
        for (std::size_t c = 0; c < count.size(); ++c) start[c + 1] = start[c] + count[c];
        std::vector<std::uint32_t> next(start.begin(), start.end() - 1);
        for (std::size_t i = 0; i < b_rows.size(); ++i) periods[next[group[i]]++] = b_rows[i].second;
        for (std::size_t i = 0; i < a_count; ++i) {
          const auto it = group_of.find(a_rows[i * a_width]->id);
          if (it == group_of.end()) continue;
          emit(i, periods.begin() + start[it->second], periods.begin() + start[it->second + 1], [](const Ent* t) {
            return t;
          });
        }
      };
      if (way == Way::kIdentity) {
        std::unordered_map<std::uint32_t, std::uint32_t> group_of;
        hashed(group_of);
      } else {
        std::unordered_map<std::uint32_t, std::uint32_t, MixedHash> group_of;
        hashed(group_of);
      }
    } else if (way == Way::kSort) {
      std::vector<std::pair<std::uint32_t, const Ent*>> sorted(b_rows.size());
      for (std::size_t i = 0; i < b_rows.size(); ++i) sorted[i] = {b_rows[i].first->id, b_rows[i].second};
      std::stable_sort(sorted.begin(), sorted.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
      for (std::size_t i = 0; i < a_count; ++i) {
        const std::uint32_t id  = a_rows[i * a_width]->id;
        const auto [first, end] = std::equal_range(sorted.begin(),
                                                   sorted.end(),
                                                   std::pair<std::uint32_t, const Ent*>{id, nullptr},
                                                   [](const auto& p, const auto& q) { return p.first < q.first; });
        emit(i, first, end, [](const auto& p) { return p.second; });
      }
    } else {
      std::map<const Ent*, std::vector<const Ent*>> periods_of;
      for (const auto& [a, t] : b_rows) periods_of[a].push_back(t);
      for (std::size_t i = 0; i < a_count; ++i) {
        const auto it = periods_of.find(a_rows[i * a_width]);
        if (it == periods_of.end()) continue;
        emit(i, it->second.begin(), it->second.end(), [](const Ent* t) { return t; });
      }
    }
  });
  r.check("join", setCheck(joined));
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

}  // namespace make
