// SPDX-License-Identifier: Apache-2.0
// catalog/data.hpp - the cell's data: entities, rows of k columns, and the samples lookups draw from
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

#include "dreamor/index_set.hpp"

// One entity type for every column: a column is told apart by its name, not by what it holds.
// serial is the entity's place in the data, id the integer a hand-written arm packs into its keys.
struct Ent {
  std::uint32_t id, serial;
};
// Variable names: <name>_<id>_..., as a hand-coder's would read.
inline std::uint32_t keyName(const Ent* e) {
  return e->id;
}

// Column j of a row: k1 (outer), k2 .. k(k-1) (by k1), and the last one, the period (by k1).
template <std::size_t J>
struct KName;
template <>
struct KName<0> {
  static constexpr dreamor::ColumnName value{"k1"};
};
template <>
struct KName<1> {
  static constexpr dreamor::ColumnName value{"k2"};
};
template <>
struct KName<2> {
  static constexpr dreamor::ColumnName value{"k3"};
};
template <>
struct KName<3> {
  static constexpr dreamor::ColumnName value{"k4"};
};
template <>
struct KName<4> {
  static constexpr dreamor::ColumnName value{"k5"};
};
template <std::size_t J>
using KCol = dreamor::Col<const Ent*, KName<J>::value>;

inline constexpr std::size_t kMaxColumns = 5;

static inline std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// The cell: the data's shape. Two-level factors keep the benchmark's names and meanings; L and I
// are the two halves of its A.
struct Cell {
  std::size_t k        = 3;        // columns
  std::size_t n_target = 1000000;  // rows
  bool owned           = false;    // O: the middle columns' entities belong to one k1 each
  bool sparse          = false;    // D: one candidate in ten is a row, by a predicate
  bool skewed          = false;    // S: Pareto list sizes with the same mean
  bool small           = false;    // G: short lists (5 middle, 10 periods) instead of long (50, 100)
  bool shuffled        = false;    // L: the lists in a random order, so rows are generated in it
  bool permuted        = false;    // I: every column's ids in a random order
  std::uint64_t seed   = 1;
};

struct Data {
  std::size_t k = 0;
  // ents[j]: column j's entities in serial order. The last column's last entity is a period in
  // no list: the absent key of the misses.
  std::array<std::vector<std::unique_ptr<Ent>>, kMaxColumns> ents;
  std::vector<const Ent*> k1s;  // the order the data is read in
  // lists[j][k1->serial] for j = 1 .. k-1: what k1 offers for column j, in list order.
  std::array<std::vector<std::vector<const Ent*>>, kMaxColumns> lists;
  const Ent* absent = nullptr;
  bool sparse       = false;
  std::size_t rows  = 0;

  [[nodiscard]] std::size_t last() const { return k - 1; }
  // A candidate row - its entities' serials - is a row when the predicate keeps it.
  template <typename Serials>
  [[nodiscard]] bool keep(const Serials& serials) const {
    if (!sparse) { return true; }
    std::uint64_t h = 0;
    for (std::size_t j = 0; j < k; ++j) {
      h = mix(h ^ (std::uint64_t{serials[j]} << (8 * j)));
    }
    return h % 10 == 0;
  }
  // The period before: one serial earlier, as orderedPeriods.prev(t) would give it.
  [[nodiscard]] const Ent* prev(const Ent* t) const {
    return t->serial == 0 ? nullptr : ents[last()][t->serial - 1].get();
  }
  // Calls f(entities of the row) for every row, in generation order: k1 by k1, then each list in
  // turn, the period innermost.
  template <typename F>
  void forEachRow(F&& f) const {
    std::array<const Ent*, kMaxColumns> row{};
    std::array<std::uint32_t, kMaxColumns> serials{};
    for (const Ent* a : k1s) {
      row[0]     = a;
      serials[0] = a->serial;
      walk(1, a->serial, row, serials, f);
    }
  }

 private:
  template <typename F>
  void walk(std::size_t j,
            std::uint32_t k1,
            std::array<const Ent*, kMaxColumns>& row,
            std::array<std::uint32_t, kMaxColumns>& serials,
            F& f) const {
    if (j >= kMaxColumns) {
      return;  // k <= kMaxColumns: never taken, but the compiler cannot tell
    }
    for (const Ent* e : lists[j][k1]) {
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

static Data makeData(const Cell& c) {
  Data d;
  d.k      = c.k;
  d.sparse = c.sparse;
  std::mt19937_64 g(c.seed * 7919 + 17);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  // Pareto (alpha = 1.5) weights with mean 1: a few k1s get long lists.
  auto weight = [&]() {
    if (!c.skewed) { return 1.0; }
    const double alpha = 1.5, xm = (alpha - 1.0) / alpha;
    return std::min(xm / std::pow(1.0 - u(g), 1.0 / alpha), 50.0);
  };
  const std::size_t last = c.k - 1;
  // Mean list sizes: the periods 100 or 10; the middle columns share 50 or 5 between them.
  const double periods = c.small ? 10 : 100;
  const double middle  = c.k > 2 ? std::pow(c.small ? 5.0 : 50.0, 1.0 / static_cast<double>(c.k - 2)) : 1.0;
  auto add             = [&d](std::size_t j) {
    const auto serial = static_cast<std::uint32_t>(d.ents[j].size());
    d.ents[j].push_back(std::make_unique<Ent>(Ent{serial, serial}));
    return d.ents[j].back().get();
  };
  const auto pool_last = static_cast<std::size_t>(periods) * 4;  // one pool of periods
  for (std::size_t i = 0; i <= pool_last; ++i) {
    add(last);
  }
  d.absent = d.ents[last].back().get();
  std::array<std::vector<const Ent*>, kMaxColumns> shared;
  if (!c.owned) {
    for (std::size_t j = 1; j < last; ++j) {
      const auto pool = static_cast<std::size_t>(std::lround(middle * 50));
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
      const double mean = j == last ? periods : middle;
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
    // Count this k1's rows.
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

  // L and I, from a stream of their own so that the rows above do not depend on them.
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
      const std::size_t count = j == last ? d.ents[j].size() - 1 : d.ents[j].size();  // the absent key keeps its id
      std::vector<std::uint32_t> ids(count);
      std::iota(ids.begin(), ids.end(), 0U);
      std::shuffle(ids.begin(), ids.end(), h);
      for (std::size_t i = 0; i < count; ++i) {
        d.ents[j][i]->id = ids[i];
      }
    }
  }
  return d;
}

// Each variable carries a tag fixed by its row's serials and its family, as its upper bound, so
// the arms are checked against each other by the tags they hand out.
template <typename Serials>
static double tagOf(const Serials& serials, std::size_t count, std::uint64_t family) {
  std::uint64_t h = family * 0x51afd7ed558ccd1ULL;
  for (std::size_t j = 0; j < count; ++j) {
    h = mix(h ^ (std::uint64_t{serials[j]} << (8 * j)));
  }
  return static_cast<double>(h % 1000003);
}

// Keys every arm looks up, drawn once outside the measurements: rows at random, each at most once
// (the hits), the same rows with the absent period (the misses), and each of those keys a hit or
// a miss at random (half: a lookup that does not know which it will be). The rows are walked once
// and shuffled, so a key is a row drawn uniformly, whatever the lists' sizes.
struct Samples {
  std::vector<std::array<const Ent*, kMaxColumns>> hit, miss, half;
};

static Samples makeSamples(const Data& d, std::size_t lookups, std::uint64_t seed) {
  Samples s;
  std::mt19937_64 g(seed * 104729 + 3);
  s.hit.reserve(d.rows);
  d.forEachRow([&s](const std::array<const Ent*, kMaxColumns>& row) { s.hit.push_back(row); });
  std::shuffle(s.hit.begin(), s.hit.end(), g);
  s.hit.resize(std::min(lookups, s.hit.size()));
  s.miss.reserve(s.hit.size());
  s.half.reserve(s.hit.size());
  for (auto row : s.hit) {
    const bool present = (g() & 1) != 0;
    if (present) { s.half.push_back(row); }
    row[d.last()] = d.absent;
    s.miss.push_back(row);
    if (!present) { s.half.push_back(row); }
  }
  return s;
}
