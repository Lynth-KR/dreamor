// SPDX-License-Identifier: Apache-2.0
// catalog/setop.hpp - the set-operation family: projection, union, intersection, difference,
// membership
//
// A is the set's rows that a predicate on (k1, period) keeps, about two in three; B is generated
// apart, from the same lists with every period moved one on, and kept by a predicate on its last
// two columns, so it overlaps A in part. Neither is measured. Phases, the same in every arm:
//   build       the set's rows
//   select      the rows projected on every column but k1, each projection once
//   unite       A then the rows of B that A does not have
//   intersect   the rows of A that B has
//   difference  the rows of A that B does not have
//   member      the set's rows that B has, through B's membership
// A result's check sums its rows' hashes and counts them, whatever their order: dreamor keeps
// first-appearance order, a merge gives key order.
//
// Hand-written arms over packed ids: std::unordered_set with the identity hash (`identity`) or a
// mixed one (`mixed`); sorted keys through <algorithm> - std::sort, std::unique,
// std::set_union, std::set_intersection, std::set_difference, std::binary_search (`merge`); and
// the usual code, a std::set of the rows' entities (`set`).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "dreamor/index_set.hpp"
#include "shared.hpp"

namespace setop {

using catalog::keyOf;
using catalog::MixedHash;
using catalog::Packer;

constexpr std::uint64_t kCountWeight = 0x9e3779b97f4a7c15ULL;

inline bool inA(std::uint32_t k1, std::uint32_t period) {
  return (k1 + period) % 3 != 0;
}
inline bool inB(std::uint32_t before_last, std::uint32_t period) {
  return (before_last + period) % 2 == 0;
}

// Each k1's periods moved one on, for B.
inline std::vector<std::vector<const Ent*>> movedPeriods(const Data& d) {
  const std::size_t pool = d.ents[d.last()].size() - 1;  // the absent period is in no list
  std::vector<std::vector<const Ent*>> moved;
  moved.reserve(d.lists[d.last()].size());
  for (const auto& l : d.lists[d.last()]) {
    std::vector<const Ent*> m;
    m.reserve(l.size());
    for (const Ent* t : l) m.push_back(d.ents[d.last()][(t->serial + 1) % pool].get());
    moved.push_back(std::move(m));
  }
  return moved;
}

// --- dreamor ---------------------------------------------------------------------------------
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
    bool operator()(KCol<0> a, KCol<kLast> t) const { return inA(a->serial, t->serial); }
  };
  struct InB {
    bool operator()(KCol<kLast - 1> b, KCol<kLast> t) const { return inB(b->serial, t->serial); }
  };
  // B's sources: the same lists, but each k1's periods moved one on.
  template <std::size_t I>
  struct ListOfB {
    const Data* d;
    const std::vector<std::vector<const Ent*>>* moved;
    std::span<const Ent* const> operator()(KCol<0> k1) const {
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
      h += keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<KCol<J>>(row)->serial...}, kK);
    }
    return h + set.size() * kCountWeight;
  }

  static void run(const Data& d, Result& r) {
    const auto moved = movedPeriods(d);
    Set sX;
    r.measure("build", [&] { sX = G::make(d); });
    const Set sA = sX.where(InA{});
    const Set sB = [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto gen = dreamor::makeSet<KCol<J>...>(d.k1s, ListOfB<I + 1>{&d, &moved}...);
      return d.sparse ? gen.where(typename G::Keep{&d}).where(InB{}).collect() : gen.where(InB{}).collect();
    }(std::make_index_sequence<kK - 1>{});
    select(decltype(shifted<1>(std::make_index_sequence<kK - 1>{})){}, sX, r);
    {
      Set out;
      r.measure("unite", [&] { out = dreamor::unite(sA, sB); });
      r.check("unite", checkRows(out));
    }
    {
      Set out;
      r.measure("intersect", [&] { out = dreamor::intersect(sA, sB); });
      r.check("intersect", checkRows(out));
    }
    {
      Set out;
      r.measure("difference", [&] { out = dreamor::difference(sA, sB); });
      r.check("difference", checkRows(out));
    }
    {
      Set out;
      r.measure("member", [&] {
        const auto in_b = sB.membership();
        out             = sX.where(in_b);
      });
      r.check("member", checkRows(out));
    }
  }

  template <std::size_t... I>
  static void select(std::index_sequence<I...>, const Set& sX, Result& r) {
    dreamor::SetOf<KCol<I>...> out;
    r.measure("select", [&] { out = sX.template select<KCol<I>...>(); });
    std::uint64_t h = 0;
    for (const auto& row : out) {
      h += keyOf(std::array<std::uint32_t, kMaxColumns>{std::get<KCol<I>>(row)->serial...}, sizeof...(I));
    }
    r.check("select", h + out.size() * kCountWeight);
  }
};

// --- hand-written ----------------------------------------------------------------------------
enum class Way { kIdentity, kMixed, kMerge, kSet };

static void runHand(Way way, const Data& d, Result& r) {
  const std::size_t k = d.k, last = d.last();
  Packer full, lead;
  full.k = k;
  lead.k = k - 1;
  for (std::size_t j = 0; j < k; ++j) full.size[j] = d.ents[j].size();
  for (std::size_t j = 1; j < k; ++j) lead.size[j - 1] = d.ents[j].size();
  if (way != Way::kSet && full.span() >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    r.skip("the ids do not pack into 64 bits");
    return;
  }
  std::array<std::vector<std::uint32_t>, kMaxColumns> serial_of;  // by id
  for (std::size_t j = 0; j < k; ++j) {
    serial_of[j].resize(d.ents[j].size());
    for (const auto& e : d.ents[j]) serial_of[j][e->id] = e->serial;
  }
  using Ids        = std::array<std::uint32_t, kMaxColumns>;
  using Ents       = std::array<const Ent*, kMaxColumns>;
  const auto idsOf = [k](const Ents& row) {
    Ids id{};
    for (std::size_t j = 0; j < k; ++j) id[j] = row[j]->id;
    return id;
  };
  const auto hashOf = [&](const Ids& id, std::size_t from, std::size_t count) {
    Ids s{};
    for (std::size_t c = 0; c < count; ++c) s[c] = serial_of[from + c][id[c]];
    return keyOf(s, count);
  };
  const auto rowHash  = [&](const Ids& id) { return hashOf(id, 0, k); };
  const auto leadHash = [&](const Ids& id) { return hashOf(id, 1, k - 1); };  // id holds columns 1..k-1

  std::vector<Ents> rows;  // the set's rows, entities (the set arm) - ids are derived from them
  std::vector<std::uint64_t> keys;
  r.measure("build", [&] {
    rows.reserve(d.rows);
    d.forEachRow([&](const Ents& row) { rows.push_back(row); });
    if (way != Way::kSet) {
      keys.reserve(rows.size());
      for (const Ents& row : rows) keys.push_back(full(idsOf(row)));
    }
  });
  // A and B, not measured.
  std::vector<Ents> a_rows, b_rows;
  for (const Ents& row : rows)
    if (inA(row[0]->serial, row[last]->serial)) a_rows.push_back(row);
  {
    const auto moved = movedPeriods(d);
    std::array<std::uint32_t, kMaxColumns> serials{};
    Ents row{};
    // The same walk as Data::forEachRow, with the moved periods.
    const auto walk = [&](auto& self, std::size_t j, std::uint32_t k1) -> void {
      const auto& list = j == last ? moved[k1] : d.lists[j][k1];
      for (const Ent* e : list) {
        row[j]     = e;
        serials[j] = e->serial;
        if (j + 1 < k) {
          self(self, j + 1, k1);
        } else if (d.keep(serials) && inB(row[last - 1]->serial, row[last]->serial)) {
          b_rows.push_back(row);
        }
      }
    };
    for (const Ent* a : d.k1s) {
      row[0]     = a;
      serials[0] = a->serial;
      walk(walk, 1, a->serial);
    }
  }
  const auto checkKeys = [&](const std::vector<std::uint64_t>& out, const Packer& p, bool projected) {
    std::uint64_t h = 0;
    for (const std::uint64_t key : out) h += projected ? leadHash(p.unpack(key)) : rowHash(p.unpack(key));
    return h + out.size() * kCountWeight;
  };
  const auto checkEnts = [&](const auto& out, std::size_t from, std::size_t count) {
    std::uint64_t h = 0;
    for (const Ents& row : out) {
      Ids s{};
      for (std::size_t c = 0; c < count; ++c) s[c] = row[from + c]->serial;
      h += keyOf(s, count);
    }
    return h + out.size() * kCountWeight;
  };
  const auto leadKey = [&](const Ents& row) {
    Ids id{};
    for (std::size_t j = 1; j < k; ++j) id[j - 1] = row[j]->id;
    return lead(id);
  };
  std::vector<std::uint64_t> a_keys, b_keys;
  for (const Ents& row : a_rows) a_keys.push_back(full(idsOf(row)));
  for (const Ents& row : b_rows) b_keys.push_back(full(idsOf(row)));

  if (way == Way::kIdentity || way == Way::kMixed) {
    const auto hashed = [&]<typename Hash>(Hash) {
      using HashSet = std::unordered_set<std::uint64_t, Hash>;
      {
        std::vector<std::uint64_t> out;
        r.measure("select", [&] {
          HashSet seen;
          seen.reserve(rows.size());
          for (const Ents& row : rows) {
            const std::uint64_t key = leadKey(row);
            if (seen.insert(key).second) out.push_back(key);
          }
        });
        r.check("select", checkKeys(out, lead, true));
      }
      {
        std::vector<std::uint64_t> out;
        r.measure("unite", [&] {
          HashSet seen;
          seen.reserve(a_keys.size() + b_keys.size());
          for (const auto* part : {&a_keys, &b_keys})
            for (const std::uint64_t key : *part)
              if (seen.insert(key).second) out.push_back(key);
        });
        r.check("unite", checkKeys(out, full, false));
      }
      const auto filtered = [&](const std::string& phase, const std::vector<std::uint64_t>& from, bool keep_in_b) {
        std::vector<std::uint64_t> out;
        r.measure(phase, [&] {
          const HashSet in_b(b_keys.begin(), b_keys.end());
          for (const std::uint64_t key : from)
            if (in_b.contains(key) == keep_in_b) out.push_back(key);
        });
        r.check(phase, checkKeys(out, full, false));
      };
      filtered("intersect", a_keys, true);
      filtered("difference", a_keys, false);
      filtered("member", keys, true);
    };
    if (way == Way::kIdentity) {
      hashed(std::hash<std::uint64_t>{});
    } else {
      hashed(MixedHash{});
    }
  } else if (way == Way::kMerge) {
    const auto sorted = [](std::vector<std::uint64_t> v) {
      std::sort(v.begin(), v.end());
      return v;
    };
    {
      std::vector<std::uint64_t> out;
      r.measure("select", [&] {
        out.reserve(rows.size());
        for (const Ents& row : rows) out.push_back(leadKey(row));
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
      });
      r.check("select", checkKeys(out, lead, true));
    }
    const auto merged = [&](const std::string& phase, auto op) {
      std::vector<std::uint64_t> out;
      r.measure(phase, [&] {
        const auto a = sorted(a_keys), b = sorted(b_keys);
        op(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
      });
      r.check(phase, checkKeys(out, full, false));
    };
    merged("unite", [](auto... args) { std::set_union(args...); });
    merged("intersect", [](auto... args) { std::set_intersection(args...); });
    merged("difference", [](auto... args) { std::set_difference(args...); });
    {
      std::vector<std::uint64_t> out;
      r.measure("member", [&] {
        const auto b = sorted(b_keys);
        for (const std::uint64_t key : keys)
          if (std::binary_search(b.begin(), b.end(), key)) out.push_back(key);
      });
      r.check("member", checkKeys(out, full, false));
    }
  } else {
    {
      std::set<Ents> out;
      r.measure("select", [&] {
        for (const Ents& row : rows) {
          Ents key{};
          std::copy_n(row.begin() + 1, k - 1, key.begin());
          out.insert(key);
        }
      });
      r.check("select", checkEnts(out, 0, k - 1));
    }
    {
      std::set<Ents> out;
      r.measure("unite", [&] {
        out.insert(a_rows.begin(), a_rows.end());
        out.insert(b_rows.begin(), b_rows.end());
      });
      r.check("unite", checkEnts(out, 0, k));
    }
    const auto filtered = [&](const std::string& phase, const std::vector<Ents>& from, bool keep_in_b) {
      std::vector<Ents> out;
      r.measure(phase, [&] {
        const std::set<Ents> in_b(b_rows.begin(), b_rows.end());
        for (const Ents& row : from)
          if (in_b.contains(row) == keep_in_b) out.push_back(row);
      });
      r.check(phase, checkEnts(out, 0, k));
    };
    filtered("intersect", a_rows, true);
    filtered("difference", a_rows, false);
    filtered("member", rows, true);
  }
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
  } else if (impl == "merge") {
    runHand(Way::kMerge, d, r);
  } else if (impl == "set") {
    runHand(Way::kSet, d, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace setop
