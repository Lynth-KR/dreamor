// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Differential test: the set layer (records' sets and ParamTables among it), sumExpr and the functions over rows
// against a reference written with nothing but the standard library - nested loops, std::set, std::map - over a few
// hundred small random cases from a fixed seed. The cases lean on the edges: empty sources, one element, every value,
// predicates that keep everything or nothing. Sources hold no value twice, as the contract asks
// (dreamor/index_set.hpp), and every set built from them must hold no row twice.
//
// With DREAMOR_DIFF_LARGE set (the release builds set it, tests/CMakeLists.txt; in a debug build the large cases take a
// run from half a second to forty), one large case comes after every kLargeEvery small cases, from a generator of its
// own, so the small cases' draws are those of a run without them: hundreds to thousands of values a column and long
// lists, sets of tens of thousands of rows - indexes that grow past the sizes whose slots carry tags, rows hashed in
// many blocks, large groups. Its second set is its first with about a quarter of each list replaced, so the two share
// many rows and differ in many. A large case probes a sample of the keys where a small one tries every key.
//
// A chain leaves unspecified which column varies fastest, so its rows are compared as a
// multiset. Everything downstream is specified to the row (select / unite / intersect /
// difference keep first occurrences, groups come in first-appearance order, ordered() is the
// set order, orderBy() a stable sort), so the reference runs on the rows dreamor actually produced
// and the results are compared in order.
//
// One test per column layout, each its own translation unit - test_differential.cpp and
// test_differential_*.cpp - so the layouts compile side by side. A lookup by columns tries the
// rows the thread's last lookups found, of every Key (dreamor/internal/backend/row_index.hpp), and where
// such a hint falls against another set's rows depends on the rows' size and alignment: 4-byte
// columns, padding, sizes no power of two. A layout names four columns A, B, C, D; the reference
// holds each value as its index 0..values, the sets as the layout's values - the integer itself (a signed one
// alternating around zero: 0, -1, 1, -2, ...), an enum of that integer, an entity of a frozen EntityVector, a text
// too long to be held inside a string (viewed or owned), a Code of the user's whose hash reads half of it.
//
// DREAMOR_DIFF_SEED and DREAMOR_DIFF_CASES replace the seed and the number of cases, for a longer
// run, and DREAMOR_DIFF_LARGE, set to 1, adds the large cases. Each one, set, is printed, and a failed case prints
// its seed, so the run can be repeated.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "dreamor/solution_table.hpp"
#include "support/check.hpp"
#include "support/column_values.hpp"
#include "support/float_compare.hpp"

namespace differential {

inline constexpr std::uint32_t kSeed     = 20261021;
inline constexpr std::size_t kCases      = 400;
inline constexpr std::size_t kValues     = 5;   // a small case's columns take values 0..kValues-1; kValues is in no set
inline constexpr std::size_t kLargeEvery = 20;  // a large case after every kLargeEvery small ones
inline constexpr std::size_t kFewestLargeValues = 256;  // a large case's columns take this many values or more ...
inline constexpr std::size_t kLargeValues =
    column_values::kValuePool;  // ... up to this many, as the layout's types allow

using Row     = std::tuple<std::size_t, std::size_t, std::size_t>;
using JoinRow = std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;

// The column values (support/column_values.hpp): the layouts name differential::Entity and differential::Code.
using column_values::Code;
using column_values::columnAt;
using column_values::Entity;
using column_values::indexOf;
using column_values::indexOfValue;
using column_values::mostValuesOf;
using column_values::valueAt;
using column_values::valuesOf;

template <typename T>
[[nodiscard]] std::vector<T> firstOccurrences(const std::vector<T>& rows) {
  std::set<T> seen;
  std::vector<T> out;
  for (const auto& row : rows) {
    if (seen.insert(row).second) { out.push_back(row); }
  }
  return out;
}

template <typename T>
[[nodiscard]] std::vector<T> sorted(std::vector<T> rows) {
  std::ranges::sort(rows);
  return rows;
}

// The size of a case: its columns take the values 0..values-1, and the value `values` is in no set.
struct Scale {
  std::size_t values;
  bool large;
};

// A list of distinct values in a random order, sized to hit the edges often: empty and single
// lists, then longer ones up to every value.
inline std::vector<std::size_t> randomList(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> shape(0, 9);
  std::uniform_int_distribution<std::size_t> longer(2, kValues);
  const std::size_t pick = shape(g);
  std::size_t size       = pick;  // 0 and 1 as they come
  if (pick >= 2) { size = longer(g); }
  std::vector<std::size_t> out(kValues);
  std::iota(out.begin(), out.end(), std::size_t{0});
  std::shuffle(out.begin(), out.end(), g);
  out.resize(size);
  return out;
}

// A large case's list: from shortest to longest distinct values of 0..values-1, in a random order.
inline std::vector<std::size_t> randomLargeList(std::mt19937& g,
                                                std::size_t values,
                                                std::size_t shortest,
                                                std::size_t longest) {
  std::uniform_int_distribution<std::size_t> size_of(std::min(shortest, values), std::min(longest, values));
  std::uniform_int_distribution<std::size_t> value_of(0, values - 1);
  const std::size_t size = size_of(g);
  std::vector<bool> taken(values);
  std::vector<std::size_t> out;
  out.reserve(size);
  while (out.size() < size) {
    const std::size_t v = value_of(g);
    if (!taken[v]) {
      taken[v] = true;
      out.push_back(v);
    }
  }
  return out;
}

// list with about one value in four replaced by a value it does not hold: the list must leave some value out.
inline std::vector<std::size_t> perturbedList(std::mt19937& g, std::vector<std::size_t> list, std::size_t values) {
  std::vector<bool> taken(values);
  for (const std::size_t v : list) {
    taken[v] = true;
  }
  std::uniform_int_distribution<std::size_t> value_of(0, values - 1);
  for (std::size_t& v : list) {
    if (g() % 4 != 0) { continue; }
    std::size_t other = value_of(g);
    while (taken[other]) {
      other = value_of(g);
    }
    taken[v]     = false;
    taken[other] = true;
    v            = other;
  }
  return list;
}

// (x + y) % modulus != residue - or a predicate that keeps everything (modulus 1, residue 1)
// or nothing (modulus 1, residue 0).
struct Rule {
  std::size_t modulus, residue;

  [[nodiscard]] bool keeps(std::size_t x, std::size_t y) const { return (x + y) % modulus != residue; }
};

inline Rule randomRule(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> modulus(1, 4);
  const std::size_t m = modulus(g);
  std::uniform_int_distribution<std::size_t> residue(0, m == 1 ? 1 : m - 1);
  return {m, residue(g)};
}

// A large case's rule keeps a half to three quarters of the rows: the small cases hold the ones that keep all or none.
inline Rule randomLargeRule(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> modulus(2, 4);
  const std::size_t m = modulus(g);
  std::uniform_int_distribution<std::size_t> residue(0, m - 1);
  return {m, residue(g)};
}

struct Data {
  std::vector<std::size_t> as, bs;
  std::vector<std::vector<std::size_t>> cs_of;  // the lateral column: cs_of[a]
  Rule ac, b_only;
};

inline Data randomData(std::mt19937& g) {
  Data d{randomList(g), randomList(g), {}, randomRule(g), randomRule(g)};
  for (std::size_t a = 0; a < kValues; ++a) {
    d.cs_of.push_back(randomList(g));
  }
  return d;
}

// A large case's lists: 16 to 64 as, 8 to 32 bs, 16 to 64 cs for each a - up to 131072 rows before the rules.
inline Data randomLargeData(std::mt19937& g, std::size_t values) {
  Data d{
      randomLargeList(g, values, 16, 64),
      randomLargeList(g, values, 8, 32),
      std::vector<std::vector<std::size_t>>(values),
      randomLargeRule(g),
      randomLargeRule(g),
  };
  for (const std::size_t a : d.as) {
    d.cs_of[a] = randomLargeList(g, values, 16, 64);
  }
  return d;
}

// A large case's second data: d's lists perturbed, each a new to d its own new list, each rule d's or a new one.
inline Data perturbedData(std::mt19937& g, const Data& d, std::size_t values) {
  Data e{
      perturbedList(g, d.as, values),
      perturbedList(g, d.bs, values),
      std::vector<std::vector<std::size_t>>(values),
      g() % 2 == 0 ? d.ac : randomLargeRule(g),
      g() % 2 == 0 ? d.b_only : randomLargeRule(g),
  };
  for (const std::size_t a : e.as) {
    e.cs_of[a] = d.cs_of[a].empty() ? randomLargeList(g, values, 16, 64) : perturbedList(g, d.cs_of[a], values);
  }
  return e;
}

// The reference chain: nested loops in the chain's order, predicates applied to full rows.
inline std::vector<Row> referenceRows(const Data& d) {
  std::vector<Row> out;
  for (const std::size_t a : d.as) {
    for (const std::size_t b : d.bs) {
      for (const std::size_t c : d.cs_of[a]) {
        if (d.ac.keeps(a, c) && d.b_only.keeps(b, 0)) { out.emplace_back(a, b, c); }
      }
    }
  }
  return out;
}

inline std::size_t terms(const IloExpr& e) {
  return e.expr_->terms.size();
}

// A solve's values as an algorithm hands them to VarTable::solution: getEnv, and getValues for an array of variables.
struct StubAlgorithm {
  IloEnv env;
  const std::unordered_map<const IloNumVarI*, double>* values;

  [[nodiscard]] IloEnv getEnv() const { return env; }

  void getValues(IloNumArray out, const IloNumVarArray& vars) const {
    for (IloInt k = 0; k < vars.getSize(); ++k) {
      out.add(values->at(vars[k].getImpl()));
    }
  }
};

// The variable's text, if it is set. The MSVC CRT deprecates getenv, so Windows takes _dupenv_s
// (logger.hpp's isEnvSet does the same).
[[nodiscard]] inline std::optional<std::string> environmentText(const char* variable) {
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

// A number from the environment, if the variable is set; one that does not read as a number
// stops the run rather than test something other than what was asked.
template <typename T>
[[nodiscard]] std::optional<T> fromEnvironment(const char* variable) {
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

// The row at position i of a set.
template <typename Set>
[[nodiscard]] const auto& rowAt(const Set& set, std::size_t i) {
  return *(set.begin() + static_cast<std::ptrdiff_t>(i));
}

// A row of Tup's columns, each holding the value of index i.
template <typename Tup>
[[nodiscard]] Tup rowOfIndex(std::size_t i) {
  return [i]<std::size_t... I>(std::index_sequence<I...>) {
    return Tup{columnAt<std::tuple_element_t<I, Tup>>(i)...};
  }(std::make_index_sequence<std::tuple_size_v<Tup>>{});
}

// An integer of a wider type that is out of the range of T but has v's low bits, so that a lookup that narrowed it
// to T before comparing would find v; for a 64-bit T, the same bits in the other signedness, or past the range.
template <typename T>
[[nodiscard]] auto outOfRange(std::int64_t v) {
  if constexpr (sizeof(T) < sizeof(std::int64_t)) {
    return v + static_cast<std::int64_t>(std::uint64_t{1} << (8 * sizeof(T)));
  } else if constexpr (std::is_signed_v<T>) {
    return v < 0 ? static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v) + (std::uint64_t{1} << 63U);
  } else {
    return -v - 1;
  }
}

// Whether A, B and C hold integers of three different types: a key may then take a plain integer of any type in each
// place.
template <typename Layout>
inline constexpr bool kPlainIntegerKeys = [] {
  using AV = Layout::A::value_type;
  using BV = Layout::B::value_type;
  using CV = Layout::C::value_type;
  return dreamor::internal::comparable_integer<AV> && dreamor::internal::comparable_integer<BV> &&
         dreamor::internal::comparable_integer<CV> && !std::is_same_v<AV, BV> && !std::is_same_v<AV, CV> &&
         !std::is_same_v<BV, CV>;
}();

// One case on Layout's columns: Layout::A, B, C, D.
template <typename Layout>
struct Differential {
  using A      = Layout::A;
  using B      = Layout::B;
  using C      = Layout::C;
  using D      = Layout::D;
  using AValue = A::value_type;
  using BValue = B::value_type;
  using CValue = C::value_type;
  using DValue = D::value_type;
  using Tup    = std::tuple<A, B, C>;

  // The most values a large case gives each column: as many as every column's type holds.
  static constexpr std::size_t kMostValues =
      std::min({mostValuesOf<AValue>(), mostValuesOf<BValue>(), mostValuesOf<CValue>(), mostValuesOf<DValue>()});

  template <typename Row4>
  [[nodiscard]] static Row plain(const Row4& row) {
    return {indexOf(std::get<A>(row)), indexOf(std::get<B>(row)), indexOf(std::get<C>(row))};
  }

  template <typename Row4>
  [[nodiscard]] static JoinRow plainJoin(const Row4& row) {
    return {indexOf(std::get<A>(row)), indexOf(std::get<B>(row)), indexOf(std::get<C>(row)), indexOf(std::get<D>(row))};
  }

  template <typename Set>
  [[nodiscard]] static std::vector<Row> rowsOf(const Set& set) {
    std::vector<Row> out;
    for (const auto& row : set) {
      out.push_back(plain(row));
    }
    return out;
  }

  template <typename Set>
  [[nodiscard]] static std::vector<JoinRow> joinRows(const Set& set) {
    std::vector<JoinRow> out;
    for (const auto& row : set) {
      out.push_back(plainJoin(row));
    }
    std::ranges::sort(out);
    return out;
  }

  // allOf ... findIf over rows - a set, or a group's rows - against plain loops over the same rows; the ones that
  // give a row are compared by its address, the first of equals. The weights are 0 to 3, so equal values are common.
  template <typename Rows>
  static void checkOverRows(const Rows& rows, const Rule& rule, const std::vector<int>& weight) {
    const auto keeps       = [&rule](B b, C c) { return rule.keeps(indexOf(b), indexOf(c)); };
    const auto value       = [&weight](A a, C c) { return weight[indexOf(a)] + weight[indexOf(c)]; };
    std::size_t kept       = 0;
    const void* first_kept = nullptr;
    const void* largest    = nullptr;
    const void* smallest   = nullptr;
    int largest_value      = 0;
    int smallest_value     = 0;
    int total              = 0;
    for (const auto& row : rows) {
      if (rule.keeps(indexOf(std::get<B>(row)), indexOf(std::get<C>(row)))) {
        ++kept;
        if (first_kept == nullptr) { first_kept = &row; }
      }
      const int v = weight[indexOf(std::get<A>(row))] + weight[indexOf(std::get<C>(row))];
      total += v;
      if (largest == nullptr || largest_value < v) {
        largest       = &row;
        largest_value = v;
      }
      if (smallest == nullptr || v < smallest_value) {
        smallest       = &row;
        smallest_value = v;
      }
    }
    DREAMOR_CHECK(dreamor::countIf(rows, keeps) == kept);
    DREAMOR_CHECK(dreamor::anyOf(rows, keeps) == (kept > 0) && dreamor::noneOf(rows, keeps) == (kept == 0));
    DREAMOR_CHECK(dreamor::allOf(rows, keeps) == (kept == rows.size()));
    DREAMOR_CHECK(static_cast<const void*>(dreamor::findIf(rows, keeps)) == first_kept);
    DREAMOR_CHECK(dreamor::sumValue(rows, value) == total);
    DREAMOR_CHECK(static_cast<const void*>(dreamor::maxElement(rows, value)) == largest);
    DREAMOR_CHECK(static_cast<const void*>(dreamor::minElement(rows, value)) == smallest);
    if (largest != nullptr) {
      DREAMOR_CHECK(dreamor::maxOf(rows, value) == largest_value && dreamor::minOf(rows, value) == smallest_value);
    }
  }

  // groupBy<By...>() and orderedWithin<By...>() of a set against groups gathered by hand from its plain rows: the
  // groups in first-appearance order, each one's rows in set order; at, slice, contains and iteration; regrouped()
  // and the Ordered's members laid out group by group; in each group the positions, the ends and the walks of up to
  // two steps either way, stopping at the group's ends or wrapping around them; absent, a row of no group.
  template <typename... By, typename Set, typename PlainRow, typename PlainOf, typename KeyOf>
  static void checkGrouping(const Set& set,
                            const std::vector<PlainRow>& rows,
                            const PlainOf& plain_of,
                            const KeyOf& key_of,
                            const std::ranges::range_value_t<Set>& absent) {
    using Key = std::invoke_result_t<const KeyOf&, const PlainRow&>;
    std::vector<Key> order;
    std::map<Key, std::vector<std::size_t>> groups;  // each group's positions in the set
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto [at, added] = groups.try_emplace(key_of(rows[i]));
      if (added) { order.push_back(at->first); }
      at->second.push_back(i);
    }
    std::vector<PlainRow> laid_out;
    laid_out.reserve(rows.size());
    for (const Key& key : order) {
      for (const std::size_t i : groups.at(key)) {
        laid_out.push_back(rows[i]);
      }
    }
    const auto plainRows = [&plain_of](const auto& range) {
      std::vector<PlainRow> out;
      for (const auto& row : range) {
        out.push_back(plain_of(row));
      }
      return out;
    };
    const auto groupRows = [&rows](const std::vector<std::size_t>& positions) {
      std::vector<PlainRow> out;
      for (const std::size_t i : positions) {
        out.push_back(rows[i]);
      }
      return out;
    };

    const auto grouped = set.template groupBy<By...>();
    DREAMOR_CHECK(grouped.size() == order.size() && grouped.empty() == order.empty());
    DREAMOR_CHECK(plainRows(grouped.regrouped()) == laid_out);
    bool grouping     = true;
    std::size_t index = 0;
    for (const auto& group : grouped) {
      grouping = grouping && index < order.size() && !group.rows.empty();
      if (!grouping) { break; }
      const std::vector<std::size_t>& positions = groups.at(order[index]);
      const auto& front                         = rowAt(set, positions.front());
      const auto at                             = grouped.at(index);
      grouping =
          grouping && plainRows(group.rows) == groupRows(positions) && plainRows(at.rows) == groupRows(positions);
      grouping = grouping && group.key == std::tuple<By...>{std::get<By>(front)...} && at.key == group.key;
      grouping = grouping && plainRows(grouped.slice(std::get<By>(front)...)) == groupRows(positions) &&
                 grouped.contains(std::get<By>(front)...) && plain_of(group.rows.back()) == rows[positions.back()];
      ++index;
    }
    grouping = grouping && index == order.size() && grouped.slice(std::get<By>(absent)...).empty() &&
               !grouped.contains(std::get<By>(absent)...);
    DREAMOR_CHECK(grouping);
    DREAMOR_CHECK_THROWS(std::out_of_range, grouped.at(order.size()));

    const auto within = set.template orderedWithin<By...>();
    DREAMOR_CHECK(plainRows(within.members()) == laid_out && within.size() == rows.size());
    const auto same = [&plain_of](const auto* got, const PlainRow* want) {
      return got == nullptr ? want == nullptr : want != nullptr && plain_of(*got) == *want;
    };
    bool walks = true;
    for (const Key& key : order) {
      const std::vector<std::size_t>& positions = groups.at(key);
      const auto count                          = static_cast<std::ptrdiff_t>(positions.size());
      const auto& front                         = rowAt(set, positions.front());
      walks = walks && plain_of(within.first(std::get<By>(front)...)) == rows[positions.front()] &&
              plain_of(within.last(std::get<By>(front)...)) == rows[positions.back()];
      for (std::size_t j = 0; j < positions.size(); ++j) {
        const auto& row     = rowAt(set, positions[j]);
        const auto expected = [&](std::ptrdiff_t steps, bool cyclic) -> const PlainRow* {
          std::ptrdiff_t target = static_cast<std::ptrdiff_t>(j) + steps;
          if (cyclic) { target = ((target % count) + count) % count; }
          if (target < 0 || target >= count) { return nullptr; }
          return &rows[positions[static_cast<std::size_t>(target)]];
        };
        walks = walks && within.positionOf(row) == j && within.isFirst(row) == (j == 0) &&
                within.isLast(row) == (j + 1 == positions.size()) && within.contains(row);
        for (std::ptrdiff_t steps = -2; steps <= 2; ++steps) {
          walks = walks && same(within.next(row, steps), expected(steps, false)) &&
                  same(within.prev(row, steps), expected(-steps, false)) &&
                  same(within.nextCyclic(row, steps), expected(steps, true)) &&
                  same(within.prevCyclic(row, steps), expected(-steps, true));
        }
        if (j + 1 < positions.size()) { walks = walks && plain_of(within.nextOf(row)) == rows[positions[j + 1]]; }
        if (j > 0) { walks = walks && plain_of(within.prevOf(row)) == rows[positions[j - 1]]; }
      }
    }
    walks = walks && !within.positionOf(absent).has_value() && !within.contains(absent) &&
            within.next(absent) == nullptr && within.nextCyclic(absent) == nullptr && !within.isFirst(absent) &&
            !within.isLast(absent);
    DREAMOR_CHECK(walks);
  }

  // Lookups by plain integers of other types than the columns', and the columns filled from them, where the three
  // key columns hold three integer types: a key's values as std::int64_t find its row in every lookup, and an integer
  // out of a column's range misses though its low bits are the value's. Columns of different integer types compare
  // by value, whatever their signs.
  template <typename Set, typename Table, typename Vars, typename Groups, typename Order>
  static void checkPlainIntegers(const Set& S,
                                 const std::vector<Row>& s,
                                 const Table& p,
                                 const Vars& x,
                                 const Groups& byA,
                                 const Order& O,
                                 const Data& d,
                                 const std::vector<Row>& on_c_reference) {
    const auto inS = S.membership();
    bool found     = true;
    bool missed    = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
      const auto& row = rowAt(S, i);
      const auto a    = static_cast<std::int64_t>(std::get<A>(row).get());
      const auto b    = static_cast<std::int64_t>(std::get<B>(row).get());
      const auto c    = static_cast<std::int64_t>(std::get<C>(row).get());
      found = found && S.contains(a, b, c) && inS(a, b, c) && !(!inS)(a, b, c) && O.positionOf(a, b, c) == i &&
              p(a, b, c) == p(row) && x.contains(a, b, c) && x(a, b, c).getImpl() == x(row).getImpl() &&
              byA.contains(a) && byA.slice(a).size() == byA.slice(std::get<A>(row)).size();
      if (b >= 0) { found = found && S.contains(a, static_cast<std::uint64_t>(b), c); }
      const auto wide_a = outOfRange<AValue>(a);
      const auto wide_b = outOfRange<BValue>(b);
      const auto wide_c = outOfRange<CValue>(c);
      missed = missed && !S.contains(wide_a, b, c) && !S.contains(a, wide_b, c) && !S.contains(a, b, wide_c) &&
               !inS(wide_a, b, c) && (!inS)(a, wide_b, c) && !p.contains(a, b, wide_c) && !x.contains(wide_a, b, c) &&
               !O.positionOf(a, wide_b, c).has_value() && !byA.contains(wide_a);
    }
    DREAMOR_CHECK(found);
    DREAMOR_CHECK(missed);

    // C filled from integers of another type, every one in C's range: a join on C, and a condition on C.
    using OtherC = std::conditional_t<std::is_same_v<CValue, std::int64_t>, std::int32_t, std::int64_t>;
    std::vector<OtherC> other_cs;
    for (const std::size_t idx : d.as) {
      other_cs.push_back(static_cast<OtherC>(valueAt<CValue>(idx)));
    }
    namespace rel = dreamor::index_set::relational;
    DREAMOR_CHECK(sorted(rowsOf(rel::from<C>(other_cs).join(S).build())) == sorted(on_c_reference));
    {
      using namespace dreamor::index_set::set_builder;
      DREAMOR_CHECK(sorted(rowsOf(of<A, B, C>(in<A, B, C>(S), in<C>(other_cs)).build())) == sorted(on_c_reference));
    }

    std::vector<Row> a_below_b;
    std::vector<Row> a_is_c;
    for (const Row& r : s) {
      const auto [ra, rb, rc] = r;
      if (std::cmp_less(valueAt<AValue>(ra), valueAt<BValue>(rb))) { a_below_b.push_back(r); }
      if (std::cmp_equal(valueAt<AValue>(ra), valueAt<CValue>(rc))) { a_is_c.push_back(r); }
    }
    const auto below = [](A a, B b) { return a < b; };
    const auto equal = [](A a, C c) { return a == c; };
    DREAMOR_CHECK(rowsOf(S.where(below)) == a_below_b && rowsOf(S.where(equal)) == a_is_c);
  }

  [[nodiscard]] static auto lateralLists(const Data& d) {
    std::vector<std::vector<CValue>> cs_of;
    cs_of.reserve(d.cs_of.size());
    for (const auto& list : d.cs_of) {
      cs_of.push_back(valuesOf<C>(list));
    }
    return cs_of;
  }

  // The chain of a fixed, a fixed and a lateral source, filtered while it generates - and by extra conditions, if any.
  template <typename... Extra>
  [[nodiscard]] static auto buildSet(const Data& d, const Extra&... extra) {
    const auto as    = valuesOf<A>(d.as);
    const auto bs    = valuesOf<B>(d.bs);
    const auto cs_of = lateralLists(d);
    const auto cs    = [&cs_of](A a) { return std::span<const CValue>(cs_of[indexOf(a)]); };
    const auto ac    = [&d](A a, C c) { return d.ac.keeps(indexOf(a), indexOf(c)); };
    const auto bok   = [&d](B b) { return d.b_only.keeps(indexOf(b), 0); };
    return dreamor::index_set::relational::from<A>(as)
        .template cross<B>(bs)
        .template lateral<C>(cs)
        .where(ac)
        .where(bok, extra...)
        .build();
  }

  // buildSet's set in set-builder notation, the generators and the head in other orders than the chain's: each b,
  // each a, each c in a's list, where ac(a, c) and bok(b), its columns c, a, b.
  [[nodiscard]] static auto buildSetNotation(const Data& d) {
    using namespace dreamor::index_set::set_builder;
    const auto as    = valuesOf<A>(d.as);
    const auto bs    = valuesOf<B>(d.bs);
    const auto cs_of = lateralLists(d);
    const auto cs    = [&cs_of](A a) { return std::span<const CValue>(cs_of[indexOf(a)]); };
    const auto ac    = [&d](A a, C c) { return d.ac.keeps(indexOf(a), indexOf(c)); };
    const auto bok   = [&d](B b) { return d.b_only.keeps(indexOf(b), 0); };
    return of<C, A, B>(in<B>(bs), in<A>(as), in<C>(cs)).where(ac, bok).build();
  }

  // buildSet's chain, then a join of u on the lateral column C and u's predicate: the join of S and u, from a
  // chain that never was a set.
  template <typename Set, typename Pred>
  [[nodiscard]] static auto buildJoined(const Data& d, const Set& u, const Pred& keep) {
    const auto as    = valuesOf<A>(d.as);
    const auto bs    = valuesOf<B>(d.bs);
    const auto cs_of = lateralLists(d);
    const auto cs    = [&cs_of](A a) { return std::span<const CValue>(cs_of[indexOf(a)]); };
    const auto ac    = [&d](A a, C c) { return d.ac.keeps(indexOf(a), indexOf(c)); };
    const auto bok   = [&d](B b) { return d.b_only.keeps(indexOf(b), 0); };
    return dreamor::index_set::relational::from<A>(as)
        .template cross<B>(bs)
        .template lateral<C>(cs)
        .where(ac)
        .where(bok)
        .join(u)
        .where(keep)
        .build();
  }

  // select(): projection, first occurrences; and select(expr): each row as the expression makes it - here a and c
  // trading values, by index, as the two columns may hold different types.
  template <typename Set>
  static void checkSelect(const Set& S, const std::vector<Row>& s) {
    std::vector<std::pair<std::size_t, std::size_t>> ac;
    ac.reserve(s.size());
    for (const Row& r : s) {
      ac.emplace_back(std::get<0>(r), std::get<2>(r));
    }
    std::vector<std::pair<std::size_t, std::size_t>> selected;
    for (const auto& row : S.template select<A, C>()) {
      selected.emplace_back(indexOf(std::get<A>(row)), indexOf(std::get<C>(row)));
    }
    DREAMOR_CHECK(selected == firstOccurrences(ac));

    std::vector<std::pair<std::size_t, std::size_t>> ca;
    ca.reserve(ac.size());
    for (const auto& [a, c] : ac) {
      ca.emplace_back(c, a);
    }
    std::vector<std::pair<std::size_t, std::size_t>> traded;
    const auto trade = [](A a, C c) { return std::tuple<A, C>{columnAt<A>(indexOf(c)), columnAt<C>(indexOf(a))}; };
    for (const auto& row : S.select(trade)) {
      traded.emplace_back(indexOf(std::get<A>(row)), indexOf(std::get<C>(row)));
    }
    DREAMOR_CHECK(traded == firstOccurrences(ca));
  }

  // IndexSet{records, row_of}: a row for each record, in the records' order - S's rows as records, reversed so the
  // order is the records' and not the chain's - and a ParamTable from the same records, each record's value on its
  // row, read by key and by a row of another set.
  template <typename Set>
  static void checkRecords(const Set& S, const std::vector<Row>& s, const std::vector<int>& weight) {
    struct Record {
      std::size_t a, b, c;
      int value;
    };

    std::vector<Record> records;
    records.reserve(s.size());
    for (const Row& r : std::views::reverse(s)) {
      const auto [ra, rb, rc] = r;
      records.push_back(Record{.a = ra, .b = rb, .c = rc, .value = weight[ra] - (2 * weight[rc])});
    }
    const auto row_of   = [](const Record& r) { return Tup{columnAt<A>(r.a), columnAt<B>(r.b), columnAt<C>(r.c)}; };
    const auto value_of = [](const Record& r) { return r.value; };
    const dreamor::IndexSet<A, B, C> R{records, row_of};
    DREAMOR_CHECK(rowsOf(R) == std::vector<Row>(s.rbegin(), s.rend()) && R == S);
    const dreamor::ParamTable<int, A, B, C> recordValue{records, row_of, value_of};
    bool values = recordValue.keys() == R && recordValue.size() == records.size();
    for (const Record& r : records) {
      values = values && recordValue(columnAt<A>(r.a), columnAt<B>(r.b), columnAt<C>(r.c)) == r.value;
    }
    for (const auto& row : S) {
      const auto [ra, rb, rc] = plain(row);
      values = values && recordValue(row) == weight[ra] - (2 * weight[rc]) && rb == indexOf(std::get<B>(row));
    }
    DREAMOR_CHECK(values);
  }

  // Values written in braces, three of the case's values drawn by local: a column crossed in, a source joined with
  // S, a generator over a new column and a condition on a column bound already.
  template <typename Set>
  static void checkBraces(const Set& S, const std::vector<Row>& s, std::size_t n, std::mt19937& local) {
    namespace rel = dreamor::index_set::relational;
    std::array<std::size_t, 3> picked{};
    std::uniform_int_distribution<std::size_t> any_value(0, n - 1);
    for (std::size_t k = 0; k < picked.size(); ++k) {
      do {
        picked[k] = any_value(local);
      } while (std::find(picked.begin(), picked.begin() + static_cast<std::ptrdiff_t>(k), picked[k]) !=
               picked.begin() + static_cast<std::ptrdiff_t>(k));
    }
    const auto [p0, p1, p2] = picked;
    std::vector<JoinRow> crossed;
    std::vector<Row> b_picked;
    std::vector<Row> c_picked;
    for (const Row& r : s) {
      const auto [ra, rb, rc] = r;
      crossed.emplace_back(ra, rb, rc, p0);
      crossed.emplace_back(ra, rb, rc, p1);
      if (rb == p0 || rb == p1 || rb == p2) { b_picked.push_back(r); }
      if (rc == p0 || rc == p1 || rc == p2) { c_picked.push_back(r); }
    }
    std::ranges::sort(crossed);
    DREAMOR_CHECK(joinRows(rel::from(S).template cross<D>({valueAt<DValue>(p0), valueAt<DValue>(p1)}).build()) ==
                  crossed);
    DREAMOR_CHECK(
        sorted(rowsOf(rel::from<B>({valueAt<BValue>(p0), valueAt<BValue>(p1), valueAt<BValue>(p2)}).join(S).build())) ==
        sorted(b_picked));
    using namespace dreamor::index_set::set_builder;
    DREAMOR_CHECK(joinRows(of<A, B, C, D>(in<A, B, C>(S), in<D>({valueAt<DValue>(p0), valueAt<DValue>(p1)})).build()) ==
                  crossed);
    DREAMOR_CHECK(sorted(rowsOf(of<A, B, C>(in<A, B, C>(S),
                                            in<C>({valueAt<CValue>(p0), valueAt<CValue>(p1), valueAt<CValue>(p2)}))
                                    .build())) == sorted(c_picked));
  }

  // orderBy(): a stable sort by the key, ascending or descending. The key reads two columns, out of
  // their declared order, and compares by index - the values' own order differs by layout. Five
  // values a column leave many rows with one key, so stability shows both ways.
  template <typename Set>
  static void checkOrderBy(const Set& S, const std::vector<Row>& s) {
    const auto key_of   = [](B b, C c) { return std::tuple{indexOf(c), indexOf(b)}; };
    const auto plainKey = [](const Row& r) { return std::tuple{std::get<2>(r), std::get<1>(r)}; };
    std::vector<Row> up = s;
    std::ranges::stable_sort(up, std::ranges::less{}, plainKey);
    std::vector<Row> down = s;
    std::ranges::stable_sort(down, std::ranges::greater{}, plainKey);
    DREAMOR_CHECK(rowsOf(S.orderBy(key_of)) == up);
    DREAMOR_CHECK(rowsOf(S.orderBy(key_of, dreamor::ascending)) == up);
    DREAMOR_CHECK(rowsOf(S.orderBy(key_of, dreamor::descending)) == down);
  }

  // A plain container of C's values named as the column: sumExpr<C> with a table and with a callable, under a
  // condition; sumValue<C> and the other functions over rows that take a column.
  template <typename Set>
  static void checkColumnContainer(IloEnv env, const Set& S, const std::vector<int>& weight) {
    const auto SC = S.template select<C>();
    std::vector<CValue> c_values;  // SC's values, last first
    for (std::size_t i = SC.size(); i > 0; --i) {
      c_values.push_back(std::get<C>(rowAt(SC, i - 1)).get());
    }
    const dreamor::concert::VarTable<C> vc{env, SC, 0, 1, ILOFLOAT, "c"};
    std::vector<const IloNumVarI*> vc_vars;
    for (const auto& [row, var] : vc) {
      vc_vars.push_back(var.getImpl());
    }
    const auto heavy        = [&weight](C c) { return weight[indexOf(c)] >= 2; };
    const auto weigh        = [&weight](C c) { return weight[indexOf(c)]; };
    const auto doubled      = [&vc](C c) { return 2 * vc(c); };
    IloExpr over            = dreamor::concert::sumExpr<C>(env, c_values, vc).build();
    IloExpr heavies         = dreamor::concert::sumExpr<C>(env, c_values, doubled).where(heavy).build();
    bool contained          = terms(over) == c_values.size() && vc_vars.size() == c_values.size();
    std::size_t heavy_count = 0;
    int total               = 0;
    int largest             = std::numeric_limits<int>::min();
    int smallest            = std::numeric_limits<int>::max();
    for (std::size_t k = 0; contained && k < c_values.size(); ++k) {
      contained   = contained && over.expr_->terms[k].first == vc_vars[c_values.size() - 1 - k];
      const int v = weight[indexOfValue(c_values[k])];
      total += v;
      largest  = std::max(largest, v);
      smallest = std::min(smallest, v);
      if (v >= 2) {
        contained = contained && heavy_count < terms(heavies) &&
                    heavies.expr_->terms[heavy_count].first == vc_vars[c_values.size() - 1 - k] &&
                    exactlyEqual(heavies.expr_->terms[heavy_count].second, 2);
        ++heavy_count;
      }
    }
    contained = contained && terms(heavies) == heavy_count;
    DREAMOR_CHECK(contained);
    DREAMOR_CHECK(dreamor::sumValue<C>(c_values, weigh) == total &&
                  dreamor::countIf<C>(c_values, heavy) == heavy_count);
    DREAMOR_CHECK(dreamor::anyOf<C>(c_values, heavy) == (heavy_count > 0) &&
                  dreamor::noneOf<C>(c_values, heavy) == (heavy_count == 0) &&
                  dreamor::allOf<C>(c_values, heavy) == (heavy_count == c_values.size()));
    if (!c_values.empty()) {
      DREAMOR_CHECK(dreamor::maxOf<C>(c_values, weigh) == largest && dreamor::minOf<C>(c_values, weigh) == smallest);
    }
    over.end();
    heavies.end();
  }

  // SolutionTable: x's i-th variable solved to i / 4, exact; read by key, by row and in order, missing for a row of
  // no set; asked twice, the second from the variables solution() gathered the first time; carried into a
  // ParamTable.
  template <typename Set>
  static void checkSolution(IloEnv env,
                            const Set& S,
                            const std::vector<Row>& s,
                            const dreamor::concert::VarTable<A, B, C>& x,
                            const std::vector<const IloNumVarI*>& x_vars,
                            const Tup& absent) {
    std::unordered_map<const IloNumVarI*, double> solved;
    for (std::size_t i = 0; i < x_vars.size(); ++i) {
      solved.emplace(x_vars[i], static_cast<double>(i) / 4);
    }
    const auto solution = x.solution(StubAlgorithm{env, &solved});
    const auto again    = x.solution(StubAlgorithm{env, &solved});
    const dreamor::ParamTable<double, A, B, C> carried{S, solution};
    bool read = solution.size() == s.size() && solution.keys() == S && again.size() == s.size();
    for (std::size_t i = 0; i < s.size(); ++i) {
      const auto& row    = rowAt(S, i);
      const double value = static_cast<double>(i) / 4;
      read = read && exactlyEqual(solution(std::get<A>(row), std::get<B>(row), std::get<C>(row)), value) &&
             exactlyEqual(solution(row), value) && exactlyEqual(again(row), value) &&
             exactlyEqual(carried(row), value) && solution.contains(row);
    }
    std::size_t at = 0;
    for (const auto& [row, value] : solution) {
      read = read && at < s.size() && plain(row) == s[at] && exactlyEqual(value, static_cast<double>(at) / 4);
      ++at;
    }
    read = read && at == s.size() && !solution.contains(absent);
    DREAMOR_CHECK(read);
    DREAMOR_CHECK_THROWS(std::out_of_range, solution(absent));
  }

  // ordered()'s walks: next / prev / cyclic with steps, nullptr off the ends and for a non-member; positions and ends;
  // nextOf / prevOf where the walk ends on a row.
  template <typename Ordered, typename Set>
  static void checkOrdered(const Ordered& O,
                           const Set& S,
                           const std::vector<Row>& s,
                           const std::map<Row, std::size_t>& position,
                           std::size_t n,
                           const Tup& absent) {
    const std::vector<Row>& members = s;
    const auto count                = static_cast<std::ptrdiff_t>(members.size());
    const auto expected             = [&](const Row& r, std::ptrdiff_t steps, bool cyclic) -> std::optional<Row> {
      const auto at = position.find(r);
      if (at == position.end()) { return std::nullopt; }
      std::ptrdiff_t target = static_cast<std::ptrdiff_t>(at->second) + steps;
      if (cyclic) { target = ((target % count) + count) % count; }
      if (target < 0 || target >= count) { return std::nullopt; }
      return members[static_cast<std::size_t>(target)];
    };
    const auto same = [](const auto* got, const std::optional<Row>& want) {
      return got == nullptr ? !want.has_value() : want.has_value() && plain(*got) == *want;
    };
    std::vector<Row> probes = members;
    probes.emplace_back(n, n, n);  // never a member
    bool walks = true;
    for (const Row& r : probes) {
      const A a = columnAt<A>(std::get<0>(r));
      const B b = columnAt<B>(std::get<1>(r));
      const C c = columnAt<C>(std::get<2>(r));
      for (std::ptrdiff_t steps = -3; steps <= 3; ++steps) {
        walks = walks && same(O.next(a, b, c, steps), expected(r, steps, false)) &&
                same(O.prev(a, b, c, steps), expected(r, -steps, false)) &&
                same(O.nextCyclic(a, b, c, steps), expected(r, steps, true)) &&
                same(O.prevCyclic(a, b, c, steps), expected(r, -steps, true));
      }
    }
    DREAMOR_CHECK(walks);
    bool positions = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
      const auto& row = rowAt(S, i);
      const A a       = std::get<A>(row);
      const B b       = std::get<B>(row);
      const C c       = std::get<C>(row);
      positions       = positions && O.positionOf(a, b, c) == i && O.positionOf(row) == i && plain(O.at(i)) == s[i] &&
                        O.isFirst(a, b, c) == (i == 0) && O.isLast(row) == (i + 1 == s.size()) && O.contains(a, b, c);
      if (i + 1 < s.size()) {
        positions = positions && plain(O.nextOf(a, b, c)) == s[i + 1] && &O.nextOf(row) == O.next(row);
      }
      if (i > 0) { positions = positions && plain(O.prevOf(a, b, c)) == s[i - 1] && &O.prevOf(row) == O.prev(row); }
      if (i + 2 < s.size()) { positions = positions && plain(O.nextOf(row, 2)) == s[i + 2]; }
      if (i >= 2) { positions = positions && plain(O.prevOf(a, b, c, 2)) == s[i - 2]; }
    }
    if (!s.empty()) {
      positions = positions && plain(O.first()) == s.front() && plain(O.last()) == s.back() && &O.first() == &O.at(0) &&
                  &O.last() == &O.at(s.size() - 1);
    }
    positions = positions && !O.positionOf(absent).has_value() && !O.isFirst(absent) && !O.isLast(absent) &&
                !O.contains(absent);
    DREAMOR_CHECK(positions);
    DREAMOR_CHECK_THROWS(std::out_of_range, O.at(s.size()));
  }

  // sumExpr: one term per row, narrowed by where(), one sum per whereEach() predicate - a row may land in both.
  template <typename Set>
  static void checkSums(IloEnv env,
                        const Set& S,
                        const std::vector<Row>& s,
                        const dreamor::concert::VarTable<A, B, C>& x,
                        std::mt19937& g) {
    const Rule keep       = randomRule(g);
    const Rule first      = randomRule(g);
    const Rule second     = randomRule(g);
    std::size_t kept      = 0;
    std::size_t in_first  = 0;
    std::size_t in_second = 0;
    for (const Row& r : s) {
      if (!keep.keeps(std::get<0>(r), std::get<1>(r))) { continue; }
      ++kept;
      if (first.keeps(std::get<1>(r), std::get<2>(r))) { ++in_first; }
      if (second.keeps(std::get<0>(r), std::get<2>(r))) { ++in_second; }
    }
    IloExpr all = dreamor::concert::sumExpr(env, S, x).build();
    DREAMOR_CHECK(terms(all) == s.size());
    all.end();
    auto split = dreamor::concert::sumExpr(env, S, x)
                     .where([&keep](A a, B b) { return keep.keeps(indexOf(a), indexOf(b)); })
                     .whereEach([&first](B b, C c) { return first.keeps(indexOf(b), indexOf(c)); },
                                [&second](A a, C c) { return second.keeps(indexOf(a), indexOf(c)); })
                     .build();
    DREAMOR_CHECK(terms(split[0]) == in_first && terms(split[1]) == in_second && in_first <= kept);
    for (auto& sum : split) {
      sum.end();
    }
  }

  // One case, small or large; returns the number of rows of the largest set it made.
  static std::size_t check(std::mt19937& g, IloEnv env, const Scale& scale) {
    namespace rel            = dreamor::index_set::relational;
    const std::size_t n      = scale.values;
    const Data d             = scale.large ? randomLargeData(g, n) : randomData(g);
    const Data e             = scale.large ? perturbedData(g, d, n) : randomData(g);
    const auto S             = buildSet(d);
    const auto T             = buildSet(e);
    const std::vector<Row> s = rowsOf(S);
    const std::vector<Row> t = rowsOf(T);
    const Tup absent         = rowOfIndex<Tup>(n);  // a row of no set

    // A chain of a fixed, a fixed and a lateral source, filtered while it generates.
    DREAMOR_CHECK(sorted(s) == sorted(referenceRows(d)));
    DREAMOR_CHECK(sorted(t) == sorted(referenceRows(e)));
    DREAMOR_CHECK(firstOccurrences(s) == s && firstOccurrences(t) == t);

    // where() on a finished set keeps the order.
    const Rule rule = randomRule(g);
    std::vector<Row> filtered;
    for (const Row& r : s) {
      if (rule.keeps(std::get<1>(r), std::get<2>(r))) { filtered.push_back(r); }
    }
    const auto keeps_bc = [&rule](B b, C c) { return rule.keeps(indexOf(b), indexOf(c)); };
    DREAMOR_CHECK(rowsOf(S.where(keeps_bc)) == filtered);

    // allOf ... findIf: over the set, and below over each group's rows. The weights come from a generator of their
    // own, seeded by the case, so drawing them does not shift the shared generator's draws; so does every draw this
    // case makes after the shared generator's own, from local.
    std::mt19937 weights_of_case{static_cast<std::uint32_t>((s.size() * 31) + t.size())};
    std::vector<int> weight(n);
    for (int& w : weight) {
      w = static_cast<int>(weights_of_case() % 4);
    }
    std::mt19937 local{weights_of_case()};
    checkOverRows(S, rule, weight);

    checkSelect(S, s);

    checkRecords(S, s, weight);

    // ParamTable{set, value}: a column callable's value for each row, another table as the callable, one value for
    // every row - read by key, by a row of the set, by a row of another set, and in order.
    const auto weighed        = [&weight](A a, C c) { return (10 * weight[indexOf(a)]) + weight[indexOf(c)]; };
    const auto expectedWeight = [&weight](const Row& r) {
      return (10 * weight[std::get<0>(r)]) + weight[std::get<2>(r)];
    };
    const dreamor::ParamTable<int, A, B, C> p{S, weighed};
    const dreamor::ParamTable<std::int64_t, A, B, C> widened{S, p};
    const dreamor::ParamTable<int, A, B, C> seven{S, 7};
    bool params = p.size() == s.size() && p.keys() == S && widened.size() == s.size() && seven.size() == s.size();
    std::size_t visited = 0;
    for (const auto& [row, value] : p) {
      params = params && visited < s.size() && plain(row) == s[visited] && value == expectedWeight(s[visited]);
      ++visited;
    }
    params = params && visited == s.size();
    for (std::size_t i = 0; i < s.size(); ++i) {
      const auto& row = rowAt(S, i);
      const int want  = expectedWeight(s[i]);
      params          = params && p(std::get<A>(row), std::get<B>(row), std::get<C>(row)) == want && p(row) == want &&
                        widened(row) == std::int64_t{want} && seven(row) == 7 && p.contains(row);
    }
    const std::set<Row> s_set(s.begin(), s.end());
    const std::set<Row> t_set(t.begin(), t.end());
    for (const auto& row : T) {
      const bool in = s_set.contains(plain(row));
      params        = params && p.contains(row) == in && (!in || p(row) == expectedWeight(plain(row)));
    }
    params = params && !p.contains(absent) && !widened.contains(absent);
    DREAMOR_CHECK(params);
    DREAMOR_CHECK_THROWS(std::out_of_range, p(absent));

    // The algebra, in order.
    std::vector<Row> both = s;
    both.insert(both.end(), t.begin(), t.end());
    std::vector<Row> in_both;
    std::vector<Row> only_s;
    for (const Row& r : s) {
      (t_set.contains(r) ? in_both : only_s).push_back(r);
    }
    DREAMOR_CHECK(rowsOf(dreamor::unite(S, T)) == firstOccurrences(both));
    DREAMOR_CHECK(rowsOf(dreamor::intersect(S, T)) == in_both);
    DREAMOR_CHECK(rowsOf(dreamor::difference(S, T)) == only_s);
    DREAMOR_CHECK(dreamor::within(S, T) == only_s.empty());
    DREAMOR_CHECK(dreamor::within(dreamor::intersect(S, T), S) && dreamor::within(dreamor::intersect(S, T), T));

    // The same algebra with an operand whose columns come in another order, matched by name: the result has the left
    // operand's columns, its rows in the same order.
    {
      const auto TR            = T.template select<C, A, B>();
      std::vector<Row> both_tr = t;
      both_tr.insert(both_tr.end(), s.begin(), s.end());
      std::vector<Row> t_in_s;
      std::vector<Row> only_t;
      for (const Row& r : t) {
        (s_set.contains(r) ? t_in_s : only_t).push_back(r);
      }
      static_assert(std::is_same_v<decltype(dreamor::unite(S, TR)), dreamor::IndexSet<A, B, C>>);
      static_assert(std::is_same_v<decltype(dreamor::unite(TR, S)), dreamor::IndexSet<C, A, B>>);
      DREAMOR_CHECK(rowsOf(dreamor::unite(S, TR)) == firstOccurrences(both));
      DREAMOR_CHECK(rowsOf(dreamor::intersect(S, TR)) == in_both && rowsOf(dreamor::difference(S, TR)) == only_s);
      DREAMOR_CHECK(rowsOf(dreamor::unite(TR, S)) == firstOccurrences(both_tr));
      DREAMOR_CHECK(rowsOf(dreamor::intersect(TR, S)) == t_in_s && rowsOf(dreamor::difference(TR, S)) == only_t);
      DREAMOR_CHECK(dreamor::within(S, TR) == only_s.empty() && dreamor::within(TR, S) == only_t.empty());
      DREAMOR_CHECK((S == TR) == (s_set == t_set) && (TR == S) == (s_set == t_set));
      DREAMOR_CHECK(S == S.template select<B, C, A>() && dreamor::within(S.template select<C, B, A>(), S));
    }

    // A chain over S and a set U sharing c: the natural join, whichever set comes first and with
    // a predicate across the two.
    const std::vector<std::size_t> u_cs = scale.large ? randomLargeList(g, n, n / 4, n / 2) : randomList(g);
    const std::vector<std::size_t> u_ds = scale.large ? randomLargeList(g, n, 1, 8) : randomList(g);
    const Rule cd                       = randomRule(g);
    const Rule ad                       = randomRule(g);
    const auto u_c_values               = valuesOf<C>(u_cs);
    const auto u_d_values               = valuesOf<D>(u_ds);
    const auto keep_cd                  = [&cd](C c, D dv) { return cd.keeps(indexOf(c), indexOf(dv)); };
    const auto U                        = rel::from<C>(u_c_values).template cross<D>(u_d_values).where(keep_cd).build();
    const auto keep_ad                  = [&ad](A a, D dv) { return ad.keeps(indexOf(a), indexOf(dv)); };
    std::map<std::size_t, std::vector<std::size_t>> u_ds_of;  // U's ds for each c
    for (const auto& u : U) {
      u_ds_of[indexOf(std::get<C>(u))].push_back(indexOf(std::get<D>(u)));
    }
    std::vector<JoinRow> join_reference;
    for (const Row& r : s) {
      const auto at = u_ds_of.find(std::get<2>(r));
      if (at == u_ds_of.end()) { continue; }
      for (const std::size_t dv : at->second) {
        if (ad.keeps(std::get<0>(r), dv)) {
          join_reference.emplace_back(std::get<0>(r), std::get<1>(r), std::get<2>(r), dv);
        }
      }
    }
    std::ranges::sort(join_reference);
    const auto SU = rel::from(S).join(U).where(keep_ad).build();
    DREAMOR_CHECK(joinRows(SU) == join_reference);
    DREAMOR_CHECK(joinRows(rel::from(U).join(S).where(keep_ad).build()) == join_reference);
    // A join on a column a source of one column brought, not a set: the lateral column C of S's own chain, and a
    // range's column C - d.as read as C's indices, so no draw shifts the cases after - then a new column after
    // the join.
    DREAMOR_CHECK(joinRows(buildJoined(d, U, keep_ad)) == join_reference);
    const auto c_range = valuesOf<C>(d.as);
    std::vector<Row> on_c_reference;
    for (const std::size_t c : d.as) {
      for (const Row& r : s) {
        if (std::get<2>(r) == c) { on_c_reference.push_back(r); }
      }
    }
    DREAMOR_CHECK(sorted(rowsOf(rel::from<C>(c_range).join(S).build())) == sorted(on_c_reference));
    std::vector<JoinRow> then_d_reference;
    for (const Row& r : on_c_reference) {
      for (const std::size_t dv : u_ds) {
        then_d_reference.emplace_back(std::get<0>(r), std::get<1>(r), std::get<2>(r), dv);
      }
    }
    std::ranges::sort(then_d_reference);
    DREAMOR_CHECK(joinRows(rel::from<C>(c_range).join(S).template cross<D>(u_d_values).build()) == then_d_reference);
    // A join whose set's columns are all bound already: a filter - a semijoin.
    const auto keeps_ac = [&rule](A a, C c) { return rule.keeps(indexOf(a), indexOf(c)); };
    const auto AC       = S.template select<A, C>().where(keeps_ac);
    std::vector<Row> semi_reference;
    for (const Row& r : s) {
      if (rule.keeps(std::get<0>(r), std::get<2>(r))) { semi_reference.push_back(r); }
    }
    DREAMOR_CHECK(sorted(rowsOf(rel::from(S).join(AC).build())) == sorted(semi_reference));

    // The same sets in set-builder notation: S itself, its head and generators in other orders; S and U met on the
    // column they share; S's c among a range of values - a condition on a column bound already, looked up.
    {
      using namespace dreamor::index_set::set_builder;
      const auto notation = buildSetNotation(d);
      static_assert(std::is_same_v<std::ranges::range_value_t<decltype(notation)>, std::tuple<C, A, B>>);
      DREAMOR_CHECK(sorted(rowsOf(notation)) == sorted(s));
      DREAMOR_CHECK(joinRows(of<A, B, C, D>(in<A, B, C>(S), in<C, D>(U)).where(keep_ad).build()) == join_reference);
      DREAMOR_CHECK(sorted(rowsOf(of<A, B, C>(in<A, B, C>(S), in<C>(c_range)).build())) == sorted(on_c_reference));
    }

    // Ranges of rows that are not sets: S's rows reversed and U's rows, as vectors of tuples - one source keeps its
    // order; joined with a set, with each other, and in set-builder notation.
    {
      std::vector<Tup> s_rows;
      s_rows.reserve(s.size());
      for (std::size_t i = s.size(); i > 0; --i) {
        s_rows.push_back(rowAt(S, i - 1));
      }
      const std::vector<std::tuple<C, D>> u_rows(U.begin(), U.end());
      const std::vector<Row> reversed_filtered(filtered.rbegin(), filtered.rend());
      DREAMOR_CHECK(rowsOf(rel::from(s_rows).build()) == std::vector<Row>(s.rbegin(), s.rend()));
      DREAMOR_CHECK(rowsOf(rel::from(s_rows).where(keeps_bc).build()) == reversed_filtered);
      DREAMOR_CHECK(joinRows(rel::from(S).join(u_rows).where(keep_ad).build()) == join_reference);
      DREAMOR_CHECK(joinRows(rel::from(s_rows).join(U).where(keep_ad).build()) == join_reference);
      DREAMOR_CHECK(joinRows(rel::from(s_rows).join(u_rows).where(keep_ad).build()) == join_reference);
      DREAMOR_CHECK(sorted(rowsOf(rel::from<C>(c_range).join(s_rows).build())) == sorted(on_c_reference));
      using namespace dreamor::index_set::set_builder;
      DREAMOR_CHECK(joinRows(of<A, B, C, D>(in<A, B, C>(s_rows), in<C, D>(u_rows)).where(keep_ad).build()) ==
                    join_reference);
    }

    checkBraces(S, s, n, local);

    // membership() and its opposite: over every possible row in a small case, over the rows of both sets and a
    // sample of keys in a large one.
    const auto inS     = S.membership();
    const auto notInS  = !inS;
    const auto inOrOut = [&](const Row& r) {
      const A a     = columnAt<A>(std::get<0>(r));
      const B b     = columnAt<B>(std::get<1>(r));
      const C c     = columnAt<C>(std::get<2>(r));
      const bool in = s_set.contains(r);
      return inS(a, b, c) == in && notInS(a, b, c) == !in && (!notInS)(a, b, c) == in;
    };
    bool membership = true;
    if (!scale.large) {
      for (std::size_t a = 0; a < n; ++a) {
        for (std::size_t b = 0; b < n; ++b) {
          for (std::size_t c = 0; c < n; ++c) {
            membership = membership && inOrOut(Row{a, b, c});
          }
        }
      }
    } else {
      std::uniform_int_distribution<std::size_t> any_value(0, n);
      for (const Row& r : s) {
        membership = membership && inOrOut(r);
      }
      for (const Row& r : t) {
        membership = membership && inOrOut(r);
      }
      for (std::size_t k = 0; k < s.size() + 64; ++k) {
        membership = membership && inOrOut(Row{any_value(local), any_value(local), any_value(local)});
      }
    }
    DREAMOR_CHECK(membership);
    // A membership of fewer columns than the rows it keeps: T's rows by whether their a and c are in AC, on a set, in
    // a chain and in set-builder notation; and a chain that keeps the rows not in S.
    std::set<std::pair<std::size_t, std::size_t>> ac_set;
    for (const auto& row : AC) {
      ac_set.emplace(indexOf(std::get<A>(row)), indexOf(std::get<C>(row)));
    }
    std::vector<Row> t_in_ac;
    std::vector<Row> t_out_ac;
    std::vector<Row> t_not_s;
    for (const Row& r : t) {
      (ac_set.contains({std::get<0>(r), std::get<2>(r)}) ? t_in_ac : t_out_ac).push_back(r);
      if (!s_set.contains(r)) { t_not_s.push_back(r); }
    }
    DREAMOR_CHECK(rowsOf(T.where(AC.membership())) == t_in_ac && rowsOf(T.where(!AC.membership())) == t_out_ac);
    DREAMOR_CHECK(sorted(rowsOf(buildSet(e, AC.membership()))) == sorted(t_in_ac));
    DREAMOR_CHECK(sorted(rowsOf(buildSet(e, !AC.membership(), notInS))) ==
                  sorted(rowsOf(T.where(!AC.membership(), notInS))));
    DREAMOR_CHECK(sorted(rowsOf(buildSet(e, notInS))) == sorted(t_not_s));
    {
      using namespace dreamor::index_set::set_builder;
      DREAMOR_CHECK(sorted(rowsOf(of<A, B, C>(in<A, B, C>(T)).where(!AC.membership()).build())) == sorted(t_out_ac));
    }

    // groupBy(): groups by first appearance, rows in set order; slice() for every key, present or not.
    std::vector<std::size_t> key_order;
    std::map<std::size_t, std::vector<Row>> groups;
    for (const Row& r : s) {
      if (!groups.contains(std::get<0>(r))) { key_order.push_back(std::get<0>(r)); }
      groups[std::get<0>(r)].push_back(r);
    }
    const auto byA = S.template groupBy<A>();
    DREAMOR_CHECK(byA.size() == key_order.size());
    for (std::size_t i = 0; i < std::min(byA.size(), key_order.size()); ++i) {
      DREAMOR_CHECK(indexOf(std::get<A>(byA.at(i).key)) == key_order[i]);
      DREAMOR_CHECK(rowsOf(byA.at(i).rows) == groups[key_order[i]]);
      checkOverRows(byA.at(i).rows, rule, weight);
    }
    bool slices = true;
    for (std::size_t a = 0; a <= n; ++a) {
      const auto at        = groups.find(a);
      const auto& expected = at == groups.end() ? std::vector<Row>{} : at->second;
      slices               = slices && rowsOf(byA.slice(columnAt<A>(a))) == expected &&
                             byA.contains(columnAt<A>(a)) == (at != groups.end()) &&
                             byA.slice(columnAt<A>(a)).size() == expected.size();
    }
    DREAMOR_CHECK(slices);
    std::map<std::pair<std::size_t, std::size_t>, std::vector<Row>> by_bc;
    for (const Row& r : s) {
      by_bc[{std::get<1>(r), std::get<2>(r)}].push_back(r);
    }
    const auto byBC = S.template groupBy<B, C>();
    DREAMOR_CHECK(byBC.size() == by_bc.size());
    bool bc_slices = true;
    for (const auto& [bc, rows] : by_bc) {
      bc_slices = bc_slices && rowsOf(byBC.slice(columnAt<B>(bc.first), columnAt<C>(bc.second))) == rows;
    }
    DREAMOR_CHECK(bc_slices);

    // groupBy and orderedWithin by one column, by two out of their declared order, and by three of the join's four.
    const auto plainOf  = [](const auto& row) { return plain(row); };
    const auto plainOf4 = [](const auto& row) { return plainJoin(row); };
    checkGrouping<A>(S, s, plainOf, [](const Row& r) { return std::get<0>(r); }, absent);
    checkGrouping<C, B>(S, s, plainOf, [](const Row& r) { return std::pair{std::get<2>(r), std::get<1>(r)}; }, absent);
    const auto W = SU.template select<A, B, C, D>();
    std::vector<JoinRow> w_rows;
    for (const auto& row : W) {
      w_rows.push_back(plainJoin(row));
    }
    checkGrouping<D, A, C>(
        W,
        w_rows,
        plainOf4,
        [](const JoinRow& r) { return std::tuple{std::get<3>(r), std::get<0>(r), std::get<2>(r)}; },
        rowOfIndex<std::tuple<A, B, C, D>>(n));

    // ordered(): the set order; positions and ends; next / prev / cyclic with steps, nullptr off the ends and for a
    // non-member; nextOf / prevOf where the walk ends on a row.
    std::map<Row, std::size_t> position;
    for (std::size_t i = 0; i < s.size(); ++i) {
      position.emplace(s[i], i);
    }
    const auto O = S.ordered();
    DREAMOR_CHECK(rowsOf(O.members()) == s && O.size() == s.size() && O.empty() == s.empty());
    checkOrdered(O, S, s, position, n, absent);

    checkOrderBy(S, s);

    const dreamor::concert::VarTable<A, B, C> x{env, S, 0, 1, ILOFLOAT, "x"};
    checkSums(env, S, s, x, g);

    // Lookups against each row's position in S, for x and a second table y on S - the two share
    // S's row index - with a table z on another set's rows in between, leaving its own rows as the
    // last found, and after each a lookup in a table w on the join's rows, another Key, so that the
    // thread's hints hold rows of both. Asked in a fixed order but for the sweep over every key,
    // whose order takes one draw, so no draw shifts the cases after this one however many lookups
    // there are: each row of S twice (contains, then the variable), its successor and its
    // predecessor; every possible key, absent ones included, in a random order (a sample of them in a large case);
    // each row by reference; and by columns inside a sum, where the sum's row answers.
    const dreamor::concert::VarTable<A, B, C> y{env, S, 0, 1, ILOFLOAT, "y"};
    const dreamor::concert::VarTable<A, B, C> z{env, T, 0, 1, ILOFLOAT, "z"};
    const dreamor::concert::VarTable<A, B, C, D> w{env, W, 0, 1, ILOFLOAT, "w"};
    std::vector<const IloNumVarI*> x_vars;
    std::vector<const IloNumVarI*> y_vars;
    std::vector<const IloNumVarI*> w_vars;
    for (const auto& [row, var] : x) {
      x_vars.push_back(var.getImpl());
    }
    for (const auto& [row, var] : y) {
      y_vars.push_back(var.getImpl());
    }
    for (const auto& [row, var] : w) {
      w_vars.push_back(var.getImpl());
    }
    // W's rows in set order, each followed by a key W does not have: a walk, broken by misses.
    std::map<JoinRow, std::size_t> w_position;
    std::vector<JoinRow> w_probes;
    for (const JoinRow& r : w_rows) {
      w_position.emplace(r, w_position.size());
      w_probes.push_back(r);
      w_probes.emplace_back(std::get<0>(r), std::get<1>(r), std::get<2>(r), n);
    }
    const auto finds = [&](const Row& r) {
      const A a     = columnAt<A>(std::get<0>(r));
      const B b     = columnAt<B>(std::get<1>(r));
      const C c     = columnAt<C>(std::get<2>(r));
      const auto at = position.find(r);
      const bool in = at != position.end();
      bool ok       = x.contains(a, b, c) == in && y.contains(a, b, c) == in;
      if (in) { ok = ok && x(a, b, c).getImpl() == x_vars[at->second] && y(a, b, c).getImpl() == y_vars[at->second]; }
      return ok && z.contains(a, b, c) == t_set.contains(r);
    };
    std::size_t next_w  = 0;
    const auto findsInW = [&] {
      if (w_probes.empty()) { return true; }
      const JoinRow& r = w_probes[next_w++ % w_probes.size()];
      const A a        = columnAt<A>(std::get<0>(r));
      const B b        = columnAt<B>(std::get<1>(r));
      const C c        = columnAt<C>(std::get<2>(r));
      const D dv       = columnAt<D>(std::get<3>(r));
      const auto at    = w_position.find(r);
      const bool in    = at != w_position.end();
      return w.contains(a, b, c, dv) == in && (!in || w(a, b, c, dv).getImpl() == w_vars[at->second]);
    };
    bool lookups = x_vars.size() == s.size() && y_vars.size() == s.size() && w_vars.size() == W.size();
    for (std::size_t i = 0; i < s.size(); ++i) {
      lookups = lookups && finds(s[i]) && findsInW();
      if (i + 1 < s.size()) { lookups = lookups && finds(s[i + 1]) && findsInW(); }
      if (i > 0) { lookups = lookups && finds(s[i - 1]) && findsInW(); }
    }
    std::vector<Row> every_key;  // a == n: never in a set
    if (!scale.large) {
      every_key.reserve((n + 1) * n * n);
      for (std::size_t a = 0; a <= n; ++a) {
        for (std::size_t b = 0; b < n; ++b) {
          for (std::size_t c = 0; c < n; ++c) {
            every_key.emplace_back(a, b, c);
          }
        }
      }
    } else {
      std::uniform_int_distribution<std::size_t> any_value(0, n);
      every_key = t;
      for (std::size_t k = 0; k < s.size() + 64; ++k) {
        every_key.emplace_back(any_value(local), any_value(local), any_value(local));
      }
    }
    std::mt19937 sweep{g()};
    std::ranges::shuffle(every_key, sweep);
    for (const Row& r : every_key) {
      lookups = lookups && finds(r) && findsInW();
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
      const auto& row = rowAt(S, i);
      lookups         = lookups && x(row).getImpl() == x_vars[i] && y(row).getImpl() == y_vars[i];
    }
    for (std::size_t i = 0; i < W.size(); ++i) {
      lookups = lookups && w(rowAt(W, i)).getImpl() == w_vars[i];
    }
    DREAMOR_CHECK(lookups);
    IloExpr twice = dreamor::concert::sumExpr(env, S, [&y](A a, B b, C c) { return 2 * y(a, b, c); }).build();
    bool in_order = terms(twice) == y_vars.size();
    for (std::size_t k = 0; in_order && k < y_vars.size(); ++k) {
      in_order = twice.expr_->terms[k].first == y_vars[k] && exactlyEqual(twice.expr_->terms[k].second, 2);
    }
    DREAMOR_CHECK(in_order);
    twice.end();

    // sumExpr over each group's slice: the group's variables in set order, and the ones a condition keeps; over T with
    // a membership of fewer columns.
    bool sliced = true;
    for (std::size_t k = 0; k < byA.size(); ++k) {
      const auto group          = byA.at(k);
      IloExpr sum               = dreamor::concert::sumExpr(env, byA.slice(std::get<A>(group.key)), x).build();
      IloExpr some              = dreamor::concert::sumExpr(env, group.rows, x).where(keeps_bc).build();
      std::size_t kept_in_group = 0;
      sliced                    = sliced && terms(sum) == group.rows.size();
      for (std::size_t j = 0; sliced && j < group.rows.size(); ++j) {
        const Row r = plain(group.rows[j]);
        sliced      = sliced && sum.expr_->terms[j].first == x_vars[position.at(r)] &&
                      exactlyEqual(sum.expr_->terms[j].second, 1);
        if (rule.keeps(std::get<1>(r), std::get<2>(r))) {
          sliced =
              sliced && kept_in_group < terms(some) && some.expr_->terms[kept_in_group].first == x_vars[position.at(r)];
          ++kept_in_group;
        }
      }
      sliced = sliced && terms(some) == kept_in_group;
      sum.end();
      some.end();
    }
    DREAMOR_CHECK(sliced);
    IloExpr in_ac = dreamor::concert::sumExpr(env, T, z).where(AC.membership()).build();
    DREAMOR_CHECK(terms(in_ac) == t_in_ac.size());
    in_ac.end();

    checkColumnContainer(env, S, weight);

    checkSolution(env, S, s, x, x_vars, absent);

    if constexpr (kPlainIntegerKeys<Layout>) { checkPlainIntegers(S, s, p, x, byA, O, d, on_c_reference); }
    return std::max({S.size(), T.size(), W.size()});
  }
};

// The whole test on Layout's columns, reported under name.
template <typename Layout>
[[nodiscard]] int run(const char* name) {
  const std::optional<std::uint32_t> seed_set = fromEnvironment<std::uint32_t>("DREAMOR_DIFF_SEED");
  const std::optional<std::size_t> cases_set  = fromEnvironment<std::size_t>("DREAMOR_DIFF_CASES");
  const std::uint32_t seed                    = seed_set.value_or(kSeed);
  const std::size_t cases                     = cases_set.value_or(kCases);
  const bool with_large                       = fromEnvironment<std::uint32_t>("DREAMOR_DIFF_LARGE").value_or(0) != 0;
  if (seed_set) { std::printf("  %s: seed %u, from DREAMOR_DIFF_SEED\n", name, seed); }
  if (cases_set) { std::printf("  %s: %zu cases, from DREAMOR_DIFF_CASES\n", name, cases); }
  if (with_large) { std::printf("  %s: large cases, from DREAMOR_DIFF_LARGE\n", name); }
  std::mt19937 g(seed);  // NOLINT(bugprone-random-generator-seed): fixed on purpose, every run checks the same cases
  constexpr std::size_t kMostValues = Differential<Layout>::kMostValues;
  std::size_t largest               = 0;
  std::size_t large_cases           = 0;
  // An environment for each run of kLargeEvery small cases and the large case after them, ended after it.
  for (std::size_t block = 0; block * kLargeEvery < cases; ++block) {
    IloEnv env;
    const std::size_t end = std::min(cases, (block + 1) * kLargeEvery);
    for (std::size_t i = block * kLargeEvery; i < end; ++i) {
      const std::size_t failures_before = g_failures;
      largest = std::max(largest, Differential<Layout>::check(g, env, Scale{.values = kValues, .large = false}));
      if (g_failures != failures_before) { std::printf("  %s: case %zu of seed %u failed\n", name, i, seed); }
    }
    if (with_large && end == (block + 1) * kLargeEvery) {
      std::seed_seq seeds{seed, static_cast<std::uint32_t>(block)};
      std::mt19937 large(seeds);
      std::uniform_int_distribution<std::size_t> values(std::min(kFewestLargeValues, kMostValues), kMostValues);
      const std::size_t failures_before = g_failures;
      largest =
          std::max(largest, Differential<Layout>::check(large, env, Scale{.values = values(large), .large = true}));
      ++large_cases;
      if (g_failures != failures_before) {
        std::printf("  %s: large case %zu (after case %zu) of seed %u failed\n", name, block, end - 1, seed);
      }
    }
    env.end();
  }
  using Key  = std::tuple<typename Layout::A, typename Layout::B, typename Layout::C>;
  using Join = std::tuple<typename Layout::A, typename Layout::B, typename Layout::C, typename Layout::D>;
  std::printf("%s: seed %u, %zu cases and %zu large, rows of %zu bytes, joined rows of %zu, largest set %zu rows\n",
              name,
              seed,
              cases,
              large_cases,
              sizeof(Key),
              sizeof(Join),
              largest);
  return ::report(name);
}

}  // namespace differential
