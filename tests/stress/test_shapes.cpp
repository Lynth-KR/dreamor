// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The set engine's situations, each reached by a chain and the same set in set-builder notation, checked over random
// data against nested loops in the order the sources are written, every condition applied to the full row. Rows are
// compared as sets: their order is the visit order's.
//
// Each shape reaches the situations its comment names, by the letters of docs/dev/differential-test.md, which also
// says why one shape a situation is enough. Each shape pins its visit order with a static_assert, because the order
// decides a source's situation.
//
// DREAMOR_DIFF_SEED and DREAMOR_DIFF_CASES replace the data's seed and the number of draws a shape.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"
#include "support/column_values.hpp"

namespace {

// An entity of the shapes' own EntityVector, made for each draw with k entities whose primary keys are 0..k-1.
struct Item {
  std::size_t id;
};

std::size_t primaryKey(const Item& item) {
  return item.id;
}

DREAMOR_VALUE_COLUMN(cA, std::size_t);
DREAMOR_VALUE_COLUMN(cB, std::size_t);
DREAMOR_VALUE_COLUMN(cC, std::size_t);
DREAMOR_VALUE_COLUMN(cD, std::size_t);
DREAMOR_VALUE_COLUMN(cX, std::size_t);   // another name over std::size_t, for a set of one column that fills cB
DREAMOR_VALUE_COLUMN(cS, std::int16_t);  // filled by an interval of int (F3); its values alternate in sign
DREAMOR_ENTITY_COLUMN(cE, Item);
DREAMOR_ENTITY_COLUMN(cF, Item);  // another name over Ref<Item>, for a set of one column that fills cE (F4)
DREAMOR_VALUE_COLUMN(cT, std::string);
DREAMOR_VALUE_COLUMN(cV, std::string_view);

using column_values::indexOf;
using column_values::texts;

inline constexpr std::uint32_t kSeed  = 20261110;
inline constexpr std::size_t kCases   = 400;
inline constexpr std::size_t kMost    = 5;   // a draw's values are 0..k-1, k from 1 to kMost
inline constexpr std::size_t kSlots   = 7;   // the lists of a lateral source, chosen by the values it reads
inline constexpr std::size_t kNoValue = 99;  // a value no list holds, for values in braces that match nothing

// --- The data, drawn as indices --------------------------------------------------------------------------------------

std::size_t drawBound(std::mt19937& g) {
  return std::uniform_int_distribution<std::size_t>(1, kMost)(g);
}

// Distinct values 0..k-1 in a random order, sized to hit the edges: empty and single lists often, then longer ones up
// to every value.
std::vector<std::size_t> randomList(std::mt19937& g, std::size_t k) {
  const std::size_t pick = std::uniform_int_distribution<std::size_t>(0, 9)(g);
  const std::size_t size = pick < 2 ? std::min(pick, k) : std::uniform_int_distribution<std::size_t>(1, k)(g);
  std::vector<std::size_t> out(k);
  for (std::size_t i = 0; i < k; ++i) {
    out[i] = i;
  }
  std::shuffle(out.begin(), out.end(), g);
  out.resize(size);
  return out;
}

bool contains(std::span<const std::size_t> list, std::size_t value) {
  return std::ranges::find(list, value) != list.end();
}

// The ends of an interval, 0..k each, the first at most the last three times in four.
struct Interval {
  std::size_t first, last;
};

Interval randomInterval(std::mt19937& g, std::size_t k) {
  std::uniform_int_distribution<std::size_t> end(0, k);
  const std::size_t one   = end(g);
  const std::size_t other = end(g);
  if (std::bernoulli_distribution{0.75}(g)) { return {std::min(one, other), std::max(one, other)}; }
  return {one, other};
}

// (w . values) % modulus != residue, or a condition that keeps every row or, now and then, none.
struct Rule {
  std::array<std::size_t, 3> weight;
  std::size_t modulus, residue;

  [[nodiscard]] bool keeps(std::initializer_list<std::size_t> values) const {
    std::size_t sum = 0;
    std::size_t i   = 0;
    for (const std::size_t v : values) {
      sum += weight.at(i++) * v;
    }
    return sum % modulus != residue;
  }
};

Rule randomRule(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> weight(1, 3);
  const std::size_t m     = std::uniform_int_distribution<std::size_t>(1, 4)(g);
  const bool none         = m == 1 && std::uniform_int_distribution<std::size_t>(0, 3)(g) == 0;
  const std::size_t fixed = none ? 0U : 1U;  // the residue when the modulus is 1
  const std::size_t r     = m != 1 ? std::uniform_int_distribution<std::size_t>(0, m - 1)(g) : fixed;
  const std::size_t w0    = weight(g);
  const std::size_t w1    = weight(g);
  return {{w0, w1, weight(g)}, m, r};
}

// The lists of a lateral source: one of kSlots, chosen by the values it reads, so that rows share lists.
struct Lists {
  std::vector<std::vector<std::size_t>> lists;

  [[nodiscard]] static std::size_t slot(std::initializer_list<std::size_t> values) {
    std::size_t at = 0;
    for (const std::size_t v : values) {
      at = (at * 5) + v + 1;
    }
    return at % kSlots;
  }

  [[nodiscard]] std::span<const std::size_t> at(std::initializer_list<std::size_t> values) const {
    return lists[slot(values)];
  }
};

Lists randomLists(std::mt19937& g, std::size_t k) {
  Lists out;
  for (std::size_t i = 0; i < kSlots; ++i) {
    out.lists.push_back(randomList(g, k));
  }
  return out;
}

// The rows of a set of two or three columns: each of the k^arity rows with one chance, in a random order.
using Row3 = std::array<std::size_t, 3>;

std::vector<Row3> randomRelation(std::mt19937& g, std::size_t k, std::size_t arity) {
  static constexpr std::array<double, 6> kChances{0.0, 0.3, 0.5, 0.7, 0.9, 1.0};
  const double chance = kChances.at(std::uniform_int_distribution<std::size_t>(0, kChances.size() - 1)(g));
  std::bernoulli_distribution keep(chance);
  std::vector<Row3> out;
  std::size_t count = 1;
  for (std::size_t i = 0; i < arity; ++i) {
    count *= k;
  }
  for (std::size_t n = 0; n < count; ++n) {
    Row3 row{};
    std::size_t rest = n;
    for (std::size_t i = 0; i < arity; ++i) {
      row.at(i) = rest % k;
      rest /= k;
    }
    if (keep(g)) { out.push_back(row); }
  }
  std::shuffle(out.begin(), out.end(), g);
  return out;
}

// --- The same data as dreamor's sources ------------------------------------------------------------------------------

// A set of one std::size_t column from a list.
template <typename C>
dreamor::IndexSet<C> oneColumn(const std::vector<std::size_t>& values) {
  const auto row_of = [](const std::size_t& v) { return std::tuple<C>{C{v}}; };
  return dreamor::IndexSet<C>{values, row_of};
}

// A set of std::size_t columns from a relation's rows.
template <typename... Cs>
dreamor::IndexSet<Cs...> setOf(const std::vector<Row3>& rows) {
  const auto row_of = [](const Row3& row) {
    return [&row]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
      return std::tuple<Cs...>{Cs{row.at(Is)}...};
    }(std::index_sequence_for<Cs...>{});
  };
  return dreamor::IndexSet<Cs...>{rows, row_of};
}

// The same rows as a range of rows that is not a set.
template <typename... Cs>
std::vector<std::tuple<Cs...>> tuplesOf(const std::vector<Row3>& rows) {
  std::vector<std::tuple<Cs...>> out;
  out.reserve(rows.size());
  for (const Row3& row : rows) {
    out.push_back([&row]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
      return std::tuple<Cs...>{Cs{row.at(Is)}...};
    }(std::index_sequence_for<Cs...>{}));
  }
  return out;
}

// The entities 0..k-1, frozen.
dreamor::EntityVector<Item> itemsOf(std::size_t k) {
  dreamor::EntityVector<Item> items;
  for (std::size_t i = 0; i < k; ++i) {
    static_cast<void>(items.emplace(Item{i}));
  }
  items.freeze();
  return items;
}

// cS's value of index i, and the indices of the values of an interval of int whose ends are the values of indices
// first and last, as dreamor::interval defines it: the values alternate in sign, so an interval of values is not one
// of indices.
std::int16_t sValue(std::size_t i) {
  return column_values::valueAt<std::int16_t>(i);
}

enum class Ends : std::uint8_t { kClosed, kClosedOpen };

std::vector<std::size_t> sIndicesBetween(Ends ends, int lo, int hi) {
  std::vector<std::size_t> out;
  for (int v = lo; v < hi || (ends == Ends::kClosed && v == hi); ++v) {
    out.push_back(column_values::indexOfValue(static_cast<std::int16_t>(v)));
  }
  return out;
}

std::vector<std::size_t> between(Ends ends, std::size_t lo, std::size_t hi) {
  std::vector<std::size_t> out;
  for (std::size_t v = lo; v < hi || (ends == Ends::kClosed && v == hi); ++v) {
    out.push_back(v);
  }
  return out;
}

// --- The check -------------------------------------------------------------------------------------------------------

template <std::size_t N>
using Rows = std::vector<std::array<std::size_t, N>>;

// A set's rows as indices, the columns in the order Cs.
template <typename... Cs, typename Set>
Rows<sizeof...(Cs)> rowsOf(const Set& set) {
  Rows<sizeof...(Cs)> out;
  for (const auto& row : set) {
    out.push_back({indexOf(std::get<Cs>(row))...});
  }
  return out;
}

template <typename Row>
std::vector<Row> sorted(std::vector<Row> rows) {
  std::ranges::sort(rows);
  return rows;
}

struct Case {
  const char* shape;
  std::uint32_t seed;
  std::size_t index;
};

// How many rows the references made, and in how many draws any: a shape whose draws are all empty checks nothing.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): counters, as g_checks is
std::size_t g_rows      = 0;
std::size_t g_non_empty = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// The chain's rows and the notation's are the reference's, as sets, and neither holds a row twice.
template <typename Row>
void check(const Case& at,
           const std::vector<Row>& chain,
           const std::vector<Row>& notation,
           std::vector<Row> reference) {
  const std::size_t failures_before = g_failures;
  g_rows += reference.size();
  g_non_empty += reference.empty() ? 0U : 1U;
  const std::vector<Row> want    = sorted(std::move(reference));
  const std::vector<Row> got     = sorted(chain);
  const std::vector<Row> got_too = sorted(notation);
  DREAMOR_CHECK(std::ranges::adjacent_find(want) == want.end());  // the test's own sources hold no value twice
  DREAMOR_CHECK(std::ranges::adjacent_find(got) == got.end() && std::ranges::adjacent_find(got_too) == got_too.end());
  DREAMOR_CHECK(got == want);
  DREAMOR_CHECK(got_too == want);
  if (g_failures != failures_before) {
    std::printf("  shape %s, case %zu of seed %u failed: %zu rows wanted, %zu from the chain, %zu from the notation\n",
                at.shape,
                at.index,
                at.seed,
                want.size(),
                got.size(),
                got_too.size());
  }
}

// --- The shapes ------------------------------------------------------------------------------------------------------

using dreamor::index_set::relational::from;

// A1 A2 D1 F1 F3: a container, then an interval of int filling an int16 column, a condition on both.
void containerInterval(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k               = drawBound(g);
  const std::vector<std::size_t> as = randomList(g, k);
  const Interval i                  = randomInterval(g, k);
  const Rule rule                   = randomRule(g);
  const int lo                      = sValue(i.first);
  const int hi                      = sValue(i.last);
  const auto keeps                  = [&rule](cA a, cS s) { return rule.keeps({a, indexOf(s)}); };
  const auto chain                  = from<cA>(as).cross<cS>(dreamor::interval::closed(lo, hi)).where(keeps);
  static_assert(decltype(chain)::visitOrder() == "cA, cS");
  const auto notation = of<cS, cA>(in<cS>(dreamor::interval::closed(lo, hi)), in<cA>(as)).where(keeps);
  static_assert(decltype(notation)::visitOrder() == "cS, cA");
  Rows<2> reference;
  for (const std::size_t a : as) {
    for (const std::size_t s : sIndicesBetween(Ends::kClosed, lo, hi)) {
      if (rule.keeps({a, s})) { reference.push_back({a, s}); }
    }
  }
  check(at, rowsOf<cA, cS>(chain.build()), rowsOf<cA, cS>(notation.build()), reference);
}

// A3 B1 D1 D3: values in braces, a lateral span, one condition on the lateral column and one on both.
void bracesSpan(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k = drawBound(g);
  const Lists t       = randomLists(g, k);
  const Rule rule_b   = randomRule(g);
  const Rule rule_ab  = randomRule(g);
  const auto list_of  = [&t](cA a) { return t.at({a}); };
  const auto keeps_b  = [&rule_b](cB b) { return rule_b.keeps({b}); };
  const auto keeps_ab = [&rule_ab](cA a, cB b) { return rule_ab.keeps({a, b}); };
  // Built in the statement that writes the braces, as values in braces ask.
  const auto chain_rows = rowsOf<cA, cB>(from<cA>({std::size_t{0}, std::size_t{2}, std::size_t{4}, kNoValue})
                                             .lateral<cB>(list_of)
                                             .where(keeps_b)
                                             .where(keeps_ab)
                                             .build());
  const auto notation_rows =
      rowsOf<cA, cB>(of<cA, cB>(in<cA>({std::size_t{0}, std::size_t{2}, std::size_t{4}, kNoValue}), in<cB>(list_of))
                         .where(keeps_ab, keeps_b)
                         .build());
  static_assert(decltype(from<cA>({std::size_t{0}}).lateral<cB>(list_of))::visitOrder() == "cA, cB");
  Rows<2> reference;
  for (const std::size_t a : {std::size_t{0}, std::size_t{2}, std::size_t{4}, kNoValue}) {
    for (const std::size_t b : t.at({a})) {
      if (rule_b.keeps({b}) && rule_ab.keeps({a, b})) { reference.push_back({a, b}); }
    }
  }
  check(at, chain_rows, notation_rows, reference);
}

// A5 B2 D1 F5: an EntityVector, a lateral interval from each entity's key, a condition on both.
void entitiesInterval(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                     = drawBound(g);
  const dreamor::EntityVector<Item> items = itemsOf(k);
  const Interval e                        = randomInterval(g, k);
  const Rule rule                         = randomRule(g);
  const auto interval_of                  = [&e](cE x) { return dreamor::interval::closedOpen(x->id, e.last); };
  const auto keeps                        = [&rule](cE x, cB b) { return rule.keeps({x->id, b}); };
  const auto chain                        = from<cE>(items).lateral<cB>(interval_of).where(keeps);
  static_assert(decltype(chain)::visitOrder() == "cE, cB");
  const auto notation = of<cB, cE>(in<cE>(items), in<cB>(interval_of)).where(keeps);
  Rows<2> reference;
  for (std::size_t x = 0; x < k; ++x) {
    for (const std::size_t b : between(Ends::kClosedOpen, x, e.last)) {
      if (rule.keeps({x, b})) { reference.push_back({x, b}); }
    }
  }
  check(at, rowsOf<cE, cB>(chain.build()), rowsOf<cE, cB>(notation.build()), reference);
}

// A4 B3 F4: a set of one entity column of another name fills cE, then a lateral set of one column of another name.
void setsOfOneColumn(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                     = drawBound(g);
  const dreamor::EntityVector<Item> items = itemsOf(k);
  const std::vector<std::size_t> fs       = randomList(g, k);
  const Lists t                           = randomLists(g, k);
  const auto ref_of                       = [&items](const std::size_t& i) { return std::tuple<cF>{cF{items.at(i)}}; };
  const dreamor::IndexSet<cF> f_set{fs, ref_of};
  std::vector<dreamor::IndexSet<cX>> x_sets;
  x_sets.reserve(t.lists.size());
  for (const auto& list : t.lists) {
    x_sets.push_back(oneColumn<cX>(list));
  }
  const auto set_of = [&x_sets](cE x) -> const dreamor::IndexSet<cX>& { return x_sets[Lists::slot({x->id})]; };
  const auto chain  = from<cE>(f_set).lateral<cB>(set_of);
  static_assert(decltype(chain)::visitOrder() == "cE, cB");
  const auto notation = of<cE, cB>(in<cE>(f_set), in<cB>(set_of));
  Rows<2> reference;
  for (const std::size_t x : fs) {
    for (const std::size_t b : t.at({x})) {
      reference.push_back({x, b});
    }
  }
  check(at, rowsOf<cE, cB>(chain.build()), rowsOf<cE, cB>(notation.build()), reference);
}

// B4 D1 F1r F2: ints fill a std::size_t column, a lateral list returned by value, then Refs fill an entity column.
void byValue(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                     = drawBound(g);
  const dreamor::EntityVector<Item> items = itemsOf(k);
  const std::vector<std::size_t> as       = randomList(g, k);
  const std::vector<std::size_t> es       = randomList(g, k);
  const Lists t                           = randomLists(g, k);
  const Rule rule                         = randomRule(g);
  std::vector<int> as_ints;
  as_ints.reserve(as.size());
  for (const std::size_t a : as) {
    as_ints.push_back(static_cast<int>(a));
  }
  std::vector<dreamor::Ref<Item>> refs;
  refs.reserve(es.size());
  for (const std::size_t e : es) {
    refs.push_back(items.at(e));
  }
  const auto copy_of = [&t](cA a) {
    const auto list = t.at({a});
    return std::vector<std::size_t>(list.begin(), list.end());
  };
  const auto keeps = [&rule](cB b) { return rule.keeps({b}); };
  const auto chain = from<cA>(as_ints).lateral<cB>(copy_of).where(keeps).cross<cE>(refs);
  static_assert(decltype(chain)::visitOrder() == "cA, cB, cE");
  const auto notation = of<cA, cB, cE>(in<cA>(as_ints), in<cB>(copy_of), in<cE>(refs)).where(keeps);
  Rows<3> reference;
  for (const std::size_t a : as) {
    for (const std::size_t b : t.at({a})) {
      for (const std::size_t e : es) {
        if (rule.keeps({b})) { reference.push_back({a, b, e}); }
      }
    }
  }
  check(at, rowsOf<cA, cB, cE>(chain.build()), rowsOf<cA, cB, cE>(notation.build()), reference);
}

// C1 (an IndexSet, then a range of rows) D2: sets scanned, conditions on the columns they bind.
void scanned(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                      = drawBound(g);
  const std::vector<std::size_t> as        = randomList(g, k);
  const std::vector<Row3> bc               = randomRelation(g, k, 2);
  const std::vector<Row3> d                = randomRelation(g, k, 1);
  const Rule rule_b                        = randomRule(g);
  const Rule rule_ad                       = randomRule(g);
  const dreamor::IndexSet<cB, cC> s_bc     = setOf<cB, cC>(bc);
  const std::vector<std::tuple<cD>> d_rows = tuplesOf<cD>(d);
  const auto keeps_b                       = [&rule_b](cB b) { return rule_b.keeps({b}); };
  const auto keeps_ad                      = [&rule_ad](cA a, cD x) { return rule_ad.keeps({a, x}); };
  const auto chain                         = from<cA>(as).cross(s_bc).cross(d_rows).where(keeps_b, keeps_ad);
  static_assert(decltype(chain)::visitOrder() == "{cB, cC}, cA, {cD}");
  const auto notation = of<cA, cB, cC, cD>(in<cA>(as), in<cB, cC>(s_bc), in<cD>(d_rows)).where(keeps_b, keeps_ad);
  Rows<4> reference;
  for (const std::size_t a : as) {
    for (const Row3& r : bc) {
      for (const Row3& x : d) {
        if (rule_b.keeps({r[0]}) && rule_ad.keeps({a, x[0]})) { reference.push_back({a, r[0], r[1], x[0]}); }
      }
    }
  }
  check(at, rowsOf<cA, cB, cC, cD>(chain.build()), rowsOf<cA, cB, cC, cD>(notation.build()), reference);
}

// C2 on one column (an IndexSet, then a range of rows) D2: each set joined on a column a container binds, adding one.
void joinedOnOne(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                           = drawBound(g);
  const std::vector<std::size_t> as             = randomList(g, k);
  const std::vector<std::size_t> ds             = randomList(g, k);
  const std::vector<Row3> ab                    = randomRelation(g, k, 2);
  const std::vector<Row3> dc                    = randomRelation(g, k, 2);
  const Rule rule                               = randomRule(g);
  const dreamor::IndexSet<cA, cB> s_ab          = setOf<cA, cB>(ab);
  const std::vector<std::tuple<cD, cC>> dc_rows = tuplesOf<cD, cC>(dc);
  const auto keeps                              = [&rule](cC c) { return rule.keeps({c}); };
  const auto chain                              = from<cA>(as).join(s_ab).cross<cD>(ds).join(dc_rows).where(keeps);
  static_assert(decltype(chain)::visitOrder() == "cA, {cA, cB}, cD, {cD, cC}");
  const auto notation = of<cA, cB, cC, cD>(in<cA>(as), in<cA, cB>(s_ab), in<cD>(ds), in<cD, cC>(dc_rows)).where(keeps);
  Rows<4> reference;
  for (const std::size_t a : as) {
    for (const Row3& r : ab) {
      if (r[0] != a) { continue; }
      for (const std::size_t x : ds) {
        for (const Row3& q : dc) {
          if (q[0] == x && rule.keeps({q[1]})) { reference.push_back({a, r[1], q[1], x}); }
        }
      }
    }
  }
  check(at, rowsOf<cA, cB, cC, cD>(chain.build()), rowsOf<cA, cB, cC, cD>(notation.build()), reference);
}

// C2 on two columns in another order than the set's (an IndexSet, then a range of rows): each joined on cB and cA,
// adding one.
void joinedOnTwo(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                                = drawBound(g);
  const std::vector<std::size_t> as                  = randomList(g, k);
  const std::vector<std::size_t> bs                  = randomList(g, k);
  const std::vector<Row3> bac                        = randomRelation(g, k, 3);
  const std::vector<Row3> bad                        = randomRelation(g, k, 3);
  const Rule rule                                    = randomRule(g);
  const dreamor::IndexSet<cB, cA, cC> s_bac          = setOf<cB, cA, cC>(bac);
  const std::vector<std::tuple<cB, cA, cD>> bad_rows = tuplesOf<cB, cA, cD>(bad);
  const auto keeps                                   = [&rule](cA a, cD x) { return rule.keeps({a, x}); };
  const auto chain = from<cA>(as).cross<cB>(bs).join(s_bac).join(bad_rows).where(keeps);
  static_assert(decltype(chain)::visitOrder() == "cA, cB, {cB, cA, cD}, {cB, cA, cC}");
  const auto notation =
      of<cA, cB, cC, cD>(in<cA>(as), in<cB>(bs), in<cB, cA, cC>(s_bac), in<cB, cA, cD>(bad_rows)).where(keeps);
  Rows<4> reference;
  for (const std::size_t a : as) {
    for (const std::size_t b : bs) {
      for (const Row3& r : bac) {
        if (r[0] != b || r[1] != a) { continue; }
        for (const Row3& q : bad) {
          if (q[0] == b && q[1] == a && rule.keeps({a, q[2]})) { reference.push_back({a, b, r[2], q[2]}); }
        }
      }
    }
  }
  check(at, rowsOf<cA, cB, cC, cD>(chain.build()), rowsOf<cA, cB, cC, cD>(notation.build()), reference);
}

// C3 (an IndexSet, then a range of rows): sets whose columns are all bound, joined as filters.
void semijoined(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                      = drawBound(g);
  const std::vector<std::size_t> as        = randomList(g, k);
  const std::vector<std::size_t> bs        = randomList(g, k);
  const std::vector<Row3> ba               = randomRelation(g, k, 2);
  const std::vector<Row3> a_only           = randomRelation(g, k, 1);
  const dreamor::IndexSet<cB, cA> s_ba     = setOf<cB, cA>(ba);
  const std::vector<std::tuple<cA>> a_rows = tuplesOf<cA>(a_only);
  const auto chain                         = from<cA>(as).cross<cB>(bs).join(s_ba).join(a_rows);
  static_assert(decltype(chain)::visitOrder() == "cA, {cA}, cB, {cB, cA}");
  const auto notation = of<cA, cB>(in<cA>(as), in<cB>(bs), in<cB, cA>(s_ba), in<cA>(a_rows));
  Rows<2> reference;
  for (const std::size_t a : as) {
    for (const std::size_t b : bs) {
      const bool in_ba = std::ranges::any_of(ba, [&](const Row3& r) { return r[0] == b && r[1] == a; });
      const bool in_a  = std::ranges::any_of(a_only, [&](const Row3& r) { return r[0] == a; });
      if (in_ba && in_a) { reference.push_back({a, b}); }
    }
  }
  check(at, rowsOf<cA, cB>(chain.build()), rowsOf<cA, cB>(notation.build()), reference);
}

// D4: no condition, no lateral, no join - a container, an interval, values in braces and a set crossed, which build()
// counts before it makes the rows.
void unconditioned(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k               = drawBound(g);
  const std::vector<std::size_t> as = randomList(g, k);
  const Interval i                  = randomInterval(g, k);
  const std::vector<Row3> d         = randomRelation(g, k, 1);
  const dreamor::IndexSet<cD> s_d   = setOf<cD>(d);
  const auto chain_rows = rowsOf<cA, cB, cC, cD>(from<cA>(as)
                                                     .cross<cB>(dreamor::interval::closedOpen(i.first, i.last))
                                                     .cross<cC>({std::size_t{1}, std::size_t{3}})
                                                     .cross(s_d)
                                                     .build());
  const auto notation_rows =
      rowsOf<cA, cB, cC, cD>(of<cA, cB, cC, cD>(in<cA>(as),
                                                in<cB>(dreamor::interval::closedOpen(i.first, i.last)),
                                                in<cC>({std::size_t{1}, std::size_t{3}}),
                                                in<cD>(s_d))
                                 .build());
  Rows<4> reference;
  for (const std::size_t a : as) {
    for (const std::size_t b : between(Ends::kClosedOpen, i.first, i.last)) {
      for (const std::size_t c : {std::size_t{1}, std::size_t{3}}) {
        for (const Row3& x : d) {
          reference.push_back({a, b, c, x[0]});
        }
      }
    }
  }
  check(at, chain_rows, notation_rows, reference);
}

// E1 E2: generators over columns bound already - a range of values on cA, a set of one column of another name on cB.
// The chain writes the same conditions as where()s.
void boundValues(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                = drawBound(g);
  const std::vector<std::size_t> as  = randomList(g, k);
  const std::vector<std::size_t> bs  = randomList(g, k);
  const std::vector<std::size_t> as2 = randomList(g, k);
  const std::vector<std::size_t> xs  = randomList(g, k);
  const dreamor::IndexSet<cX> x_set  = oneColumn<cX>(xs);
  const auto in_as2                  = [&as2](cA a) { return contains(as2, a); };
  const auto in_xs                   = [&xs](cB b) { return contains(xs, b); };
  const auto chain                   = from<cA>(as).cross<cB>(bs).where(in_as2, in_xs);
  const auto notation                = of<cA, cB>(in<cA>(as), in<cB>(bs), in<cA>(as2), in<cB>(x_set));
  static_assert(decltype(notation)::visitOrder() == "cA, {cA}, cB, {cB}");
  Rows<2> reference;
  for (const std::size_t a : as) {
    for (const std::size_t b : bs) {
      if (contains(as2, a) && contains(xs, b)) { reference.push_back({a, b}); }
    }
  }
  check(at, rowsOf<cA, cB>(chain.build()), rowsOf<cA, cB>(notation.build()), reference);
}

// E3 E4 E5 F8: callable generators over columns bound already - ints for a std::size_t column, an interval, a set of
// one column of another name. The chain writes the same conditions as where()s.
void boundCallables(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k               = drawBound(g);
  const std::vector<std::size_t> as = randomList(g, k);
  const std::vector<std::size_t> bs = randomList(g, k);
  const Lists t                     = randomLists(g, k);
  const Lists u                     = randomLists(g, k);
  const Interval e                  = randomInterval(g, k);
  std::vector<std::vector<int>> t_ints;
  t_ints.reserve(t.lists.size());
  // By hand: cl.exe 19.51 warns C4267 (size_t to int) inside vector's range constructor.
  for (const auto& list : t.lists) {
    std::vector<int>& ints = t_ints.emplace_back();
    for (const std::size_t value : list) {
      ints.push_back(static_cast<int>(value));
    }
  }
  std::vector<dreamor::IndexSet<cX>> u_sets;
  u_sets.reserve(u.lists.size());
  for (const auto& list : u.lists) {
    u_sets.push_back(oneColumn<cX>(list));
  }
  const auto ints_of     = [&t_ints](cA a) { return std::span<const int>(t_ints[Lists::slot({a})]); };
  const auto interval_of = [&e](cB b) { return dreamor::interval::closedOpen(b.get(), e.last); };
  const auto set_of      = [&u_sets](cA a) -> const dreamor::IndexSet<cX>& { return u_sets[Lists::slot({a})]; };
  const auto among       = [&t, &u, &e](cA a, cB b) {
    return contains(t.at({a}), b) && b <= a && a < e.last && contains(u.at({a}), b);
  };
  const auto chain    = from<cA>(as).cross<cB>(bs).where(among);
  const auto notation = of<cA, cB>(in<cA>(as), in<cB>(bs), in<cB>(ints_of), in<cA>(interval_of), in<cB>(set_of));
  static_assert(decltype(notation)::visitOrder() == "cA, cB");
  Rows<2> reference;
  for (const std::size_t a : as) {
    for (const std::size_t b : bs) {
      if (contains(t.at({a}), b) && b <= a && a < e.last && contains(u.at({a}), b)) { reference.push_back({a, b}); }
    }
  }
  check(at, rowsOf<cA, cB>(chain.build()), rowsOf<cA, cB>(notation.build()), reference);
}

// E6 F6 F7: a temporary range of values over a column bound already, string_views filling a std::string column, string
// literals filling a std::string_view column - built in the statement that writes the temporary and the braces.
void temporariesAndText(std::mt19937& g, const Case& at) {
  using namespace dreamor::index_set::set_builder;
  const std::size_t k                = drawBound(g);
  const std::vector<std::size_t> as  = randomList(g, k);
  const std::vector<std::size_t> as2 = randomList(g, k);
  const std::vector<std::size_t> ts  = randomList(g, k);
  std::vector<std::string_view> t_views;
  t_views.reserve(ts.size());
  for (const std::size_t i : ts) {
    t_views.emplace_back(texts()[i]);
  }
  const auto in_as2     = [&as2](cA a) { return contains(as2, a); };
  const auto chain_rows = rowsOf<cA, cT, cV>(
      from<cA>(as).where(in_as2).cross<cT>(t_views).cross<cV>({texts()[1].c_str(), texts()[3].c_str()}).build());
  const auto notation_rows = rowsOf<cA, cT, cV>(of<cA, cT, cV>(in<cA>(as),
                                                               in<cA>(std::vector<std::size_t>(as2)),
                                                               in<cT>(t_views),
                                                               in<cV>({texts()[1].c_str(), texts()[3].c_str()}))
                                                    .build());
  Rows<3> reference;
  for (const std::size_t a : as) {
    for (const std::size_t i : ts) {
      for (const std::size_t v : {std::size_t{1}, std::size_t{3}}) {
        if (contains(as2, a)) { reference.push_back({a, i, v}); }
      }
    }
  }
  check(at, chain_rows, notation_rows, reference);
}

// --- The run ---------------------------------------------------------------------------------------------------------

// The variable's text, if it is set. The MSVC CRT deprecates getenv, so Windows takes _dupenv_s.
std::optional<std::string> environmentText(const char* variable) {
#ifdef _WIN32
  char* value      = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, variable) != 0 || value == nullptr) {
    std::free(value);
    return std::nullopt;
  }
  std::string text{value};
  std::free(value);
  return text;
#else
  const char* const value = std::getenv(variable);
  if (value == nullptr) { return std::nullopt; }
  return std::string{value};
#endif
}

// A number from the environment, if the variable is set; one that does not read as a number stops the run.
template <typename T>
std::optional<T> fromEnvironment(const char* variable) {
  const std::optional<std::string> text = environmentText(variable);
  if (!text.has_value()) { return std::nullopt; }
  const std::string_view view{*text};
  T value{};
  const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
  if (view.empty() || error != std::errc{} || end != view.data() + view.size()) {
    throw std::invalid_argument(std::string{variable} + " is not a number: \"" + std::string{view} + "\"");
  }
  return value;
}

struct Shape {
  const char* name;
  void (*run)(std::mt19937&, const Case&);
};

constexpr std::array kShapes{
    Shape{"containerInterval", &containerInterval},
    Shape{"bracesSpan", &bracesSpan},
    Shape{"entitiesInterval", &entitiesInterval},
    Shape{"setsOfOneColumn", &setsOfOneColumn},
    Shape{"byValue", &byValue},
    Shape{"scanned", &scanned},
    Shape{"joinedOnOne", &joinedOnOne},
    Shape{"joinedOnTwo", &joinedOnTwo},
    Shape{"semijoined", &semijoined},
    Shape{"unconditioned", &unconditioned},
    Shape{"boundValues", &boundValues},
    Shape{"boundCallables", &boundCallables},
    Shape{"temporariesAndText", &temporariesAndText},
};

}  // namespace

int main() try {
  const std::optional<std::uint32_t> seed_set = fromEnvironment<std::uint32_t>("DREAMOR_DIFF_SEED");
  const std::optional<std::size_t> cases_set  = fromEnvironment<std::size_t>("DREAMOR_DIFF_CASES");
  const std::uint32_t seed                    = seed_set.value_or(kSeed);
  const std::size_t cases                     = cases_set.value_or(kCases);
  if (seed_set) { std::printf("  shapes: seed %u, from DREAMOR_DIFF_SEED\n", seed); }
  if (cases_set) { std::printf("  shapes: %zu cases, from DREAMOR_DIFF_CASES\n", cases); }
  for (std::size_t s = 0; s < kShapes.size(); ++s) {
    const Shape& shape = kShapes.at(s);
    // A generator a shape, seeded by the seed and the shape's number, so a shape's draws do not depend on the others.
    std::seed_seq seeds{seed, static_cast<std::uint32_t>(s)};
    std::mt19937 g(seeds);
    const std::size_t rows_before = g_rows;
    for (std::size_t c = 0; c < cases; ++c) {
      shape.run(g, Case{.shape = shape.name, .seed = seed, .index = c});
    }
    // A shape that makes no row in any draw checks nothing: its data or conditions leave every set empty.
    if (cases >= kCases && g_rows == rows_before) {
      ++g_failures;
      std::printf("  shape %s made no row in %zu draws of seed %u\n", shape.name, cases, seed);
    }
  }
  std::printf("shapes: seed %u, %zu shapes, %zu cases each, %zu of them with rows, %zu rows\n",
              seed,
              kShapes.size(),
              cases,
              g_non_empty,
              g_rows);
  return ::report("shapes");
} catch (...) { return ::uncaught(); }
