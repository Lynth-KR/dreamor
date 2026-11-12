// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/data.hpp - the data of a cell: entities, rows of k columns, and the samples that lookups draw from.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// build.sh compiles this header into both builds: ./catalog, the dreamor arm from dreamor.cpp, and ./catalog_hand,
// the hand-written arms from hand.cpp. The hand-written build has no include path to dreamor, so that a change to the
// library leaves that build as it is. So this header includes no dreamor header, and what differs by arm comes from
// the arm's trait.

// The entity of every column. Columns are told apart by their names, not by the type that they hold. serial is the
// entity's place in the array of its column, and id is the integer that a hand-written arm packs into its keys.
struct Ent {
  std::uint32_t id, serial;
};

// The primary key of an entity is its id. So dreamor names a variable <name>_<id>_..., as the hand-written arms do.
inline std::uint32_t primaryKey(const Ent& e) {
  return e.id;
}

// A row holds an Arm::Handle for each of its entities. The handle refers into Arm::Ents, the array of the column's
// entities in serial order. The handle is a Ref in the dreamor arm, and a pointer in the hand-written arms. The trait
// of an arm, in dreamor/arm.hpp or hand/arm.hpp, holds the two types and the three functions that settle() calls:
// - add(ents, e) puts a copy of e at the back of ents.
// - freeze(ents) ends the filling of ents.
// - handle(ents, e) returns the handle of e, which must be an element of ents.

// The widest k that this build runs. The hand-written arms and the samples hold a row in an array this wide, with
// the columns past k null. So the width is part of what those arms cost. build.sh builds ./catalog with width 5, for
// k <= 5, and ./catalog8 with width 8, for k = 8 alone. So a run at k <= 5 does not pay for the columns that only
// k = 8 needs, and a run at k = 8 has no column to spare.
#ifndef CATALOG_MAX_COLUMNS
#  define CATALOG_MAX_COLUMNS 5
#endif
inline constexpr std::size_t kMaxColumns = CATALOG_MAX_COLUMNS;
static_assert(kMaxColumns == 5 || kMaxColumns == 8);

static inline std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// A cell: the shape of the data, as two-level factors (docs/bench/catalog/design.md, "Factors"). L and I split one
// factor, the order of the data, in two.
struct Cell {
  std::size_t k        = 3;        // The number of columns: 1, 2, 3, 5 or 8.
  std::size_t n_target = 1000000;  // The number of rows to reach.
  bool owned           = false;    // O: whether each entity of a middle column belongs to one k1.
  bool sparse          = false;    // D: whether a predicate keeps one candidate row in ten.
  bool skewed          = false;    // S: whether list sizes follow a Pareto law around the same mean.
  bool small           = false;    // G: whether lists are short, 5 middle combinations and 10 periods, not 50 and 100.
  bool shuffled        = false;    // L: whether the lists are in a random order, so that rows are generated in it.
  bool permuted        = false;    // I: whether the ids of every column are in a random order.
  std::uint64_t seed   = 1;
};

// Checks whether a candidate row, given as the serials of its entities, is a row. If the cell is dense, every
// candidate is a row. If it is sparse, a hash of the serials keeps about one candidate in ten.
template <typename Serials>
[[nodiscard]] inline bool keepRow(bool sparse, std::size_t k, const Serials& serials) {
  if (!sparse) { return true; }
  std::uint64_t h = 0;
  for (std::size_t j = 0; j < k; ++j) {
    h = mix(h ^ (std::uint64_t{serials[j]} << (8 * j)));
  }
  return h % 10 == 0;
}

// The data while it is drawn. Each entity is allocated on its own, so that the lists can point to it while more
// entities are added. makeData permutes the ids here. settle() then puts the entities of each column in one array,
// which every arm reads.
struct Draft {
  std::size_t k = 0;
  std::array<std::vector<std::unique_ptr<Ent>>, kMaxColumns> ents;
  std::vector<const Ent*> k1s;
  std::array<std::vector<std::vector<const Ent*>>, kMaxColumns> lists;
  const Ent* absent = nullptr;
  std::vector<const Ent*> absents;
  bool sparse      = false;
  std::size_t rows = 0;

  template <typename Serials>
  [[nodiscard]] bool keep(const Serials& serials) const {
    return keepRow(sparse, k, serials);
  }
};

template <typename Arm>
struct DataOf {
  using Ents   = typename Arm::Ents;
  using Handle = typename Arm::Handle;

  std::size_t k = 0;
  // ents[j] holds the entities of column j in serial order, and handles[j][serial] holds the handle of each. If
  // k > 1, the last entity of the last column is a period in no list, and absent refers to it. At k = 1, the k1s in
  // no row come after the k1s of the rows, and absents refers to them.
  std::array<Ents, kMaxColumns> ents;
  std::array<std::vector<Handle>, kMaxColumns> handles;
  std::vector<Handle> k1s;  // The k1s, in the order in which the rows are generated.
  // lists[j][k1->serial], for j from 1 to k - 1: the entities that k1 offers for column j, in list order.
  std::array<std::vector<std::vector<Handle>>, kMaxColumns> lists;
  Handle absent{};
  std::vector<Handle> absents;  // At k = 1, a k1 in no row for each row: the keys of the misses.
  bool sparse      = false;
  std::size_t rows = 0;

  [[nodiscard]] std::size_t last() const { return k - 1; }

  template <typename Serials>
  [[nodiscard]] bool keep(const Serials& serials) const {
    return keepRow(sparse, k, serials);
  }

  // Returns the period one serial before t, or an empty handle if t is the first period. Ordered::prev over the
  // periods returns the same period.
  [[nodiscard]] Handle prev(Handle t) const { return t->serial == 0 ? Handle{} : handles[last()][t->serial - 1]; }

  // Calls f with the entities of each row, in generation order: k1 by k1, then the lists of the k1 in nested loops,
  // with the period innermost. If the cell is sparse, f sees only the rows that the predicate keeps.
  template <typename F>
  void forEachRow(F&& f) const {
    std::array<Handle, kMaxColumns> row{};
    std::array<std::uint32_t, kMaxColumns> serials{};
    for (const Handle a : k1s) {
      row[0]     = a;
      serials[0] = a->serial;
      if (k == 1) {
        if (keep(serials)) { f(row); }
      } else {
        walk(1, a->serial, row, serials, f);
      }
    }
  }

 private:
  template <typename F>
  void walk(std::size_t j,
            std::uint32_t k1,
            std::array<Handle, kMaxColumns>& row,
            std::array<std::uint32_t, kMaxColumns>& serials,
            F& f) const {
    if (j >= kMaxColumns) {
      return;  // Never taken, because k <= kMaxColumns. The compiler cannot prove that, so the branch tells it.
    }
    for (const Handle e : lists[j][k1]) {
      row[j]     = e;
      serials[j] = e->serial;
      if (j + 1 < k) {
        walk(j + 1, k1, row, serials, f);
      } else if (keep(serials)) {
        f(row);
      }
    }
  }
};

// Returns the data of an arm: the entities of each column in one array, in serial order, and each reference to an
// entity turned into the arm's handle.
template <typename Arm>
static DataOf<Arm> settle(const Draft& draft) {
  using Handle = typename Arm::Handle;
  DataOf<Arm> d;
  d.k      = draft.k;
  d.sparse = draft.sparse;
  d.rows   = draft.rows;
  for (std::size_t j = 0; j < kMaxColumns; ++j) {
    d.ents[j].reserve(draft.ents[j].size());
    for (const auto& e : draft.ents[j]) {
      Arm::add(d.ents[j], *e);
    }
    Arm::freeze(d.ents[j]);
    d.handles[j].reserve(d.ents[j].size());
    for (const Ent& e : d.ents[j]) {
      d.handles[j].push_back(Arm::handle(d.ents[j], e));
    }
  }
  const auto handleOf = [&d](std::size_t j, const Ent* e) { return d.handles[j][e->serial]; };
  for (const Ent* a : draft.k1s) {
    d.k1s.push_back(handleOf(0, a));
  }
  for (std::size_t j = 1; j < draft.k; ++j) {
    for (const auto& list : draft.lists[j]) {
      std::vector<Handle>& to = d.lists[j].emplace_back();
      to.reserve(list.size());
      for (const Ent* e : list) {
        to.push_back(handleOf(j, e));
      }
    }
  }
  d.absent = handleOf(d.last(), draft.absent);
  for (const Ent* e : draft.absents) {
    d.absents.push_back(handleOf(0, e));
  }
  return d;
}

// Returns the mean list size of the middle column j, for j from 1 to k - 2. Together the middle columns make about 50
// combinations, or 5 on small lists. At k = 3 and k = 5 each column takes the (k - 2)th root of that number. At k = 5
// the list sizes round to 4, which makes 4 x 4 x 4 = 64 combinations, and to 2 on small lists, which makes
// 2 x 2 x 2 = 8. At k = 8 the root would round to 2 and 1, which makes 64 and 1. So k = 8 takes the same 64 and 8 as
// k = 5: 2 on every column, or, on small lists, 2 on the first three columns and 1 on the rest. Then k = 5 and k = 8
// differ in their columns alone.
static double middleMean(const Cell& c, std::size_t j) {
  if (c.k == 8) { return c.small && j > 3 ? 1.0 : 2.0; }
  return c.k > 2 ? std::pow(c.small ? 5.0 : 50.0, 1.0 / static_cast<double>(c.k - 2)) : 1.0;
}

// Returns the data at k = 1, where the rows are the k1s alone. If the cell is sparse, a k1 is a row only if the
// predicate keeps it. One more k1 for each row is in no row, and these k1s are the keys of the misses. A single absent
// key would be one slot, in cache from the second miss on. At k > 1 the misses spread over the k1s of the rows, and at
// k = 1 they spread over these k1s. The list factors O, S, G and L have nothing to act on.
template <typename Arm>
static DataOf<Arm> makeSingle(const Cell& c) {
  Draft d;
  d.k      = 1;
  d.sparse = c.sparse;
  std::array<std::uint32_t, kMaxColumns> serials{};
  while (d.rows < c.n_target) {
    const auto serial = static_cast<std::uint32_t>(d.ents[0].size());
    d.ents[0].push_back(std::make_unique<Ent>(Ent{serial, serial}));
    d.k1s.push_back(d.ents[0].back().get());
    serials[0] = serial;
    d.rows += d.keep(serials);
  }
  const std::size_t count = d.ents[0].size();
  for (std::size_t i = 0; i < d.rows; ++i) {
    const auto serial = static_cast<std::uint32_t>(d.ents[0].size());
    d.ents[0].push_back(std::make_unique<Ent>(Ent{serial, serial}));
    d.absents.push_back(d.ents[0].back().get());
  }
  d.absent = d.absents.front();
  if (c.permuted) {
    std::mt19937_64 h(c.seed * 6151 + 29);  // The k1s in absents keep their ids.
    std::vector<std::uint32_t> ids(count);
    std::iota(ids.begin(), ids.end(), 0U);
    std::shuffle(ids.begin(), ids.end(), h);
    for (std::size_t i = 0; i < count; ++i) {
      d.ents[0][i]->id = ids[i];
    }
  }
  return settle<Arm>(d);
}

template <typename Arm>
static DataOf<Arm> makeData(const Cell& c) {
  if (c.k == 1) { return makeSingle<Arm>(c); }
  Draft d;
  d.k      = c.k;
  d.sparse = c.sparse;
  std::mt19937_64 g(c.seed * 7919 + 17);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  // Returns the weight of a list's size: 1, or, if the cell is skewed, a draw from a Pareto law with alpha = 1.5 and
  // mean 1, capped at 50. The Pareto weights give a few k1s long lists.
  auto weight = [&]() {
    if (!c.skewed) { return 1.0; }
    const double alpha = 1.5, xm = (alpha - 1.0) / alpha;
    return std::min(xm / std::pow(1.0 - u(g), 1.0 / alpha), 50.0);
  };
  const std::size_t last = c.k - 1;
  // The mean list size of the periods is 100, or 10 on small lists. middleMean gives the middle columns' sizes.
  const double periods = c.small ? 10 : 100;
  auto add             = [&d](std::size_t j) {
    const auto serial = static_cast<std::uint32_t>(d.ents[j].size());
    d.ents[j].push_back(std::make_unique<Ent>(Ent{serial, serial}));
    return d.ents[j].back().get();
  };
  // Every list of periods draws from one pool of this many periods. One more period, the absent one, follows them.
  const auto pool_last = static_cast<std::size_t>(periods) * 4;
  for (std::size_t i = 0; i <= pool_last; ++i) {
    add(last);
  }
  d.absent = d.ents[last].back().get();
  std::array<std::vector<const Ent*>, kMaxColumns> shared;
  if (!c.owned) {
    for (std::size_t j = 1; j < last; ++j) {
      const auto pool = static_cast<std::size_t>(std::lround(middleMean(c, j) * 50));
      for (std::size_t i = 0; i < pool; ++i) {
        shared[j].push_back(add(j));
      }
    }
  }
  for (std::size_t j = 1; j < c.k; ++j) {
    d.lists[j].clear();
  }

  std::size_t rows = 0;
  std::array<std::uint32_t, kMaxColumns> serials{};
  while (rows < c.n_target) {
    const Ent* k1 = add(0);
    d.k1s.push_back(k1);
    for (std::size_t j = 1; j < c.k; ++j) {
      const double mean = j == last ? periods : middleMean(c, j);
      std::size_t n     = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(mean * weight())));
      std::vector<const Ent*> list;
      if (j == last) {
        n                       = std::min(n, pool_last);
        const std::size_t start = mix(k1->serial) % pool_last;
        for (std::size_t i = 0; i < n; ++i) {
          list.push_back(d.ents[last][(start + i) % pool_last].get());
        }
      } else if (c.owned) {
        for (std::size_t i = 0; i < n; ++i) {
          list.push_back(add(j));
        }
      } else {
        n                       = std::min(n, shared[j].size());
        const std::size_t start = (std::size_t{k1->serial} * 7) % shared[j].size();
        for (std::size_t i = 0; i < n; ++i) {
          list.push_back(shared[j][(start + i) % shared[j].size()]);
        }
      }
      d.lists[j].push_back(std::move(list));
    }
    // Counts the candidate rows of this k1 that are rows.
    serials[0] = k1->serial;
    std::array<std::size_t, kMaxColumns> at{};
    for (;;) {
      for (std::size_t j = 1; j < c.k; ++j) {
        serials[j] = d.lists[j][k1->serial][at[j]]->serial;
      }
      rows += d.keep(serials);
      std::size_t j = c.k - 1;
      while (j >= 1 && ++at[j] == d.lists[j][k1->serial].size()) {
        at[j--] = 0;
      }
      if (j == 0) { break; }
    }
  }
  d.rows = rows;

  // L and I draw from a random stream of their own, so that the lists drawn above do not depend on them.
  std::mt19937_64 h(c.seed * 6151 + 29);
  if (c.shuffled) {
    for (std::size_t j = 1; j < c.k; ++j) {
      for (auto& l : d.lists[j]) {
        std::shuffle(l.begin(), l.end(), h);
      }
    }
  }
  if (c.permuted) {
    for (std::size_t j = 0; j < c.k; ++j) {
      // The absent period, the last entity of the last column, keeps its id.
      const std::size_t count = j == last ? d.ents[j].size() - 1 : d.ents[j].size();
      std::vector<std::uint32_t> ids(count);
      std::iota(ids.begin(), ids.end(), 0U);
      std::shuffle(ids.begin(), ids.end(), h);
      for (std::size_t i = 0; i < count; ++i) {
        d.ents[j][i]->id = ids[i];
      }
    }
  }
  return settle<Arm>(d);
}

// Returns the tag of a variable, a number made from the serials of its row and from its family. Each variable carries
// its tag as its upper bound, so that the arms can be checked against each other by the tags that they hand out.
template <typename Serials>
static double tagOf(const Serials& serials, std::size_t count, std::uint64_t family) {
  std::uint64_t h = family * 0x51afd7ed558ccd1ULL;
  for (std::size_t j = 0; j < count; ++j) {
    h = mix(h ^ (std::uint64_t{serials[j]} << (8 * j)));
  }
  return static_cast<double>(h % 1000003);
}

// The keys that every arm looks up, drawn once before any phase is measured:
// - hit holds rows drawn at random, each at most once. The rows are walked once and shuffled, so a key is a row drawn
//   uniformly, whatever the sizes of the lists.
// - miss holds the same rows with the absent period. At k = 1, each miss is a different k1 in no row, in a random
//   order.
// - half holds, for each hit, the hit or its miss, at random. So a lookup of these keys cannot know which it finds.
template <typename Arm>
struct SamplesOf {
  std::vector<std::array<typename Arm::Handle, kMaxColumns>> hit, miss, half;
};

template <typename Arm>
static SamplesOf<Arm> makeSamples(const DataOf<Arm>& d, std::size_t lookups, std::uint64_t seed) {
  using Handle = typename Arm::Handle;
  SamplesOf<Arm> s;
  std::mt19937_64 g(seed * 104729 + 3);
  s.hit.reserve(d.rows);
  d.forEachRow([&s](const std::array<Handle, kMaxColumns>& row) { s.hit.push_back(row); });
  std::shuffle(s.hit.begin(), s.hit.end(), g);
  s.hit.resize(std::min(lookups, s.hit.size()));
  s.miss.reserve(s.hit.size());
  s.half.reserve(s.hit.size());
  std::vector<Handle> absents = d.absents;
  // A stream of its own, not g, so that the hits and half do not depend on how the absent k1s are shuffled.
  std::mt19937_64 h(seed * 7727 + 5);
  std::shuffle(absents.begin(), absents.end(), h);
  for (std::size_t i = 0; i < s.hit.size(); ++i) {
    auto row           = s.hit[i];
    const bool present = (g() & 1) != 0;
    if (present) { s.half.push_back(row); }
    row[d.last()] = d.k == 1 ? absents[i] : d.absent;
    s.miss.push_back(row);
    if (!present) { s.half.push_back(row); }
  }
  return s;
}
