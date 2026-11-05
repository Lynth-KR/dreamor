// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Differential test: the set layer and sumOf against a reference written with nothing but the
// standard library - nested loops, std::set, std::map - over a few hundred small random cases
// from a fixed seed. The cases lean on the edges: empty sources, one element, every value,
// predicates that keep everything or nothing. Sources hold no value twice, as the contract asks
// (dreamor/index_set.hpp), and every set built from them must hold no row twice.
//
// makeSet leaves unspecified which column varies fastest, so its rows are compared as a
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
// holds each value as its index 0..kValues, the sets as the layout's values - the integer itself,
// an entity of a fixed array, a text too long to be held inside the string.
//
// DREAMOR_DIFF_SEED and DREAMOR_DIFF_CASES replace the seed and the number of cases, for a longer
// run. Either one, set, is printed, and a failed case prints its seed, so the run can be repeated.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"

namespace differential {

inline constexpr std::uint32_t kSeed = 20261021;
inline constexpr std::size_t kCases  = 400;
inline constexpr std::size_t kValues = 5;  // every column takes values 0..kValues-1; kValues is in no set

using Row     = std::tuple<std::size_t, std::size_t, std::size_t>;
using JoinRow = std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;

// The values of a pointer column: entities of a fixed array, each knowing its index.
struct Entity {
  std::size_t id;
};
inline std::size_t keyName(const Entity* entity) {
  return entity->id;
}
inline const std::array<Entity, kValues + 1>& entities() {
  static const std::array<Entity, kValues + 1> all = [] {
    std::array<Entity, kValues + 1> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
      out[i].id = i;
    }
    return out;
  }();
  return all;
}

// The values of a text column: longer than a short string holds inline, so each row owns memory.
inline const std::array<std::string, kValues + 1>& texts() {
  static const std::array<std::string, kValues + 1> all = [] {
    std::array<std::string, kValues + 1> out;
    for (std::size_t i = 0; i < out.size(); ++i) {
      out[i] = std::string(24, 'v') + std::to_string(i);
    }
    return out;
  }();
  return all;
}

// A column's value for index i, and back.
template <typename T>
[[nodiscard]] T valueAt(std::size_t i) {
  if constexpr (std::is_integral_v<T>) {
    return static_cast<T>(i);
  } else if constexpr (std::is_pointer_v<T>) {
    static_assert(std::is_same_v<T, const Entity*>, "a pointer column holds a const Entity*");
    return &entities()[i];
  } else {
    static_assert(std::is_same_v<T, std::string>, "a column holds an integer, a const Entity* or a std::string");
    return texts()[i];
  }
}
template <typename Column>
[[nodiscard]] Column columnAt(std::size_t i) {
  using Value = Column::Value;
  return Column{valueAt<Value>(i)};
}
template <typename Column>
[[nodiscard]] std::size_t indexOf(const Column& column) {
  using Value       = Column::Value;
  const Value value = column.get();
  if constexpr (std::is_integral_v<Value>) {
    return static_cast<std::size_t>(value);
  } else if constexpr (std::is_pointer_v<Value>) {
    return value->id;
  } else {
    return static_cast<std::size_t>(std::ranges::find(texts(), value) - texts().begin());
  }
}
template <typename Column>
[[nodiscard]] auto valuesOf(const std::vector<std::size_t>& indices) {
  using Value = Column::Value;
  std::vector<Value> out;
  out.reserve(indices.size());
  for (const std::size_t i : indices) {
    out.push_back(valueAt<Value>(i));
  }
  return out;
}

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

struct Data {
  std::vector<std::size_t> as, bs;
  std::vector<std::vector<std::size_t>> cs_of;  // the dependent column: cs_of[a]
  Rule ac, b_only;
};
inline Data randomData(std::mt19937& g) {
  Data d{randomList(g), randomList(g), {}, randomRule(g), randomRule(g)};
  for (std::size_t a = 0; a < kValues; ++a) {
    d.cs_of.push_back(randomList(g));
  }
  return d;
}

// The reference makeSet: nested loops in declaration order, predicates applied to full rows.
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

// One case on Layout's columns: Layout::A, B, C, D.
template <typename Layout>
struct Differential {
  using A      = Layout::A;
  using B      = Layout::B;
  using C      = Layout::C;
  using D      = Layout::D;
  using CValue = C::Value;

  template <typename Tup>
  [[nodiscard]] static Row plain(const Tup& row) {
    return {indexOf(std::get<A>(row)), indexOf(std::get<B>(row)), indexOf(std::get<C>(row))};
  }

  template <typename Tup>
  [[nodiscard]] static JoinRow plainJoin(const Tup& row) {
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

  [[nodiscard]] static auto buildSet(const Data& d) {
    const auto as = valuesOf<A>(d.as);
    const auto bs = valuesOf<B>(d.bs);
    std::vector<std::vector<CValue>> cs_of;
    cs_of.reserve(d.cs_of.size());
    for (const auto& list : d.cs_of) {
      cs_of.push_back(valuesOf<C>(list));
    }
    const auto cs  = [&cs_of](A a) { return std::span<const CValue>(cs_of[indexOf(a)]); };
    const auto ac  = [&d](A a, C c) { return d.ac.keeps(indexOf(a), indexOf(c)); };
    const auto bok = [&d](B b) { return d.b_only.keeps(indexOf(b), 0); };
    return dreamor::makeSet<A, B, C>(as, bs, cs).where(ac).where(bok).collect();
  }

  static void check(std::mt19937& g, IloEnv env) {
    const Data d             = randomData(g);
    const Data e             = randomData(g);
    const auto S             = buildSet(d);
    const auto T             = buildSet(e);
    const std::vector<Row> s = rowsOf(S);
    const std::vector<Row> t = rowsOf(T);

    // makeSet with a fixed, a fixed and a dependent source, filtered while it generates.
    DREAMOR_CHECK(sorted(s) == sorted(referenceRows(d)));
    DREAMOR_CHECK(sorted(t) == sorted(referenceRows(e)));
    DREAMOR_CHECK(firstOccurrences(s) == s && firstOccurrences(t) == t);

    // where() on a finished set keeps the order.
    const Rule rule = randomRule(g);
    std::vector<Row> filtered;
    for (const Row& r : s) {
      if (rule.keeps(std::get<1>(r), std::get<2>(r))) { filtered.push_back(r); }
    }
    DREAMOR_CHECK(rowsOf(S.where([&rule](B b, C c) { return rule.keeps(indexOf(b), indexOf(c)); })) == filtered);

    // select(): projection, first occurrences.
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

    // select(expr): each row as the expression makes it - here a and c trading values, by index,
    // as the two columns may hold different types.
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

    // The algebra, in order.
    std::vector<Row> both = s;
    both.insert(both.end(), t.begin(), t.end());
    const std::set<Row> s_set(s.begin(), s.end());
    const std::set<Row> t_set(t.begin(), t.end());
    std::vector<Row> in_both;
    std::vector<Row> only_s;
    for (const Row& r : s) {
      (t_set.contains(r) ? in_both : only_s).push_back(r);
    }
    DREAMOR_CHECK(rowsOf(dreamor::unite(S, T)) == firstOccurrences(both));
    DREAMOR_CHECK(rowsOf(dreamor::intersect(S, T)) == in_both);
    DREAMOR_CHECK(rowsOf(dreamor::difference(S, T)) == only_s);

    // makeSet over S and a set U sharing c: the natural join, whichever set comes first and with
    // a predicate across the two.
    const std::vector<std::size_t> u_cs = randomList(g);
    const std::vector<std::size_t> u_ds = randomList(g);
    const Rule cd                       = randomRule(g);
    const Rule ad                       = randomRule(g);
    const auto u_c_values               = valuesOf<C>(u_cs);
    const auto u_d_values               = valuesOf<D>(u_ds);
    const auto U                        = dreamor::makeSet<C, D>(u_c_values, u_d_values)
                                              .where([&cd](C c, D dv) { return cd.keeps(indexOf(c), indexOf(dv)); })
                                              .collect();
    const auto keep_ad                  = [&ad](A a, D dv) { return ad.keeps(indexOf(a), indexOf(dv)); };
    std::vector<JoinRow> join_reference;
    for (const Row& r : s) {
      for (const auto& u : U) {
        if (indexOf(std::get<C>(u)) == std::get<2>(r) && ad.keeps(std::get<0>(r), indexOf(std::get<D>(u)))) {
          join_reference.emplace_back(std::get<0>(r), std::get<1>(r), std::get<2>(r), indexOf(std::get<D>(u)));
        }
      }
    }
    const auto joinRows = [](const auto& set) {
      std::vector<JoinRow> out;
      for (const auto& row : set) {
        out.push_back(plainJoin(row));
      }
      std::ranges::sort(out);
      return out;
    };
    std::ranges::sort(join_reference);
    const auto SU = dreamor::makeSet<>(S, U).where(keep_ad).collect();
    DREAMOR_CHECK(joinRows(SU) == join_reference);
    DREAMOR_CHECK(joinRows(dreamor::makeSet<>(U, S).where(keep_ad).collect()) == join_reference);

    // membership() over every possible row.
    const auto inS = S.membership();
    for (std::size_t a = 0; a < kValues; ++a) {
      for (std::size_t b = 0; b < kValues; ++b) {
        for (std::size_t c = 0; c < kValues; ++c) {
          DREAMOR_CHECK(inS(columnAt<A>(a), columnAt<B>(b), columnAt<C>(c)) == s_set.contains(Row{a, b, c}));
        }
      }
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
      DREAMOR_CHECK(indexOf(std::get<A>(byA.keyAt(i))) == key_order[i]);
      DREAMOR_CHECK(rowsOf(byA.at(i)) == groups[key_order[i]]);
    }
    for (std::size_t a = 0; a < kValues; ++a) {
      const auto expected = groups.contains(a) ? groups[a] : std::vector<Row>{};
      DREAMOR_CHECK(rowsOf(byA.slice(columnAt<A>(a))) == expected);
      DREAMOR_CHECK(byA.contains(columnAt<A>(a)) == groups.contains(a) &&
                    byA.sliceSize(columnAt<A>(a)) == expected.size());
    }
    std::map<std::pair<std::size_t, std::size_t>, std::vector<Row>> by_bc;
    for (const Row& r : s) {
      by_bc[{std::get<1>(r), std::get<2>(r)}].push_back(r);
    }
    const auto byBC = S.template groupBy<B, C>();
    DREAMOR_CHECK(byBC.size() == by_bc.size());
    for (const auto& [bc, rows] : by_bc) {
      DREAMOR_CHECK(rowsOf(byBC.slice(columnAt<B>(bc.first), columnAt<C>(bc.second))) == rows);
    }

    // ordered(): the set order; next / prev / cyclic with steps, nullptr off the ends and for a
    // non-member.
    const std::vector<Row>& members = s;
    const auto O                    = S.ordered();
    DREAMOR_CHECK(rowsOf(O.members()) == members);
    const auto count    = static_cast<std::ptrdiff_t>(members.size());
    const auto expected = [&](const Row& r, std::ptrdiff_t steps, bool cyclic) -> std::optional<Row> {
      const auto at = std::ranges::find(members, r);
      if (at == members.end()) { return std::nullopt; }
      std::ptrdiff_t target = (at - members.begin()) + steps;
      if (cyclic) { target = ((target % count) + count) % count; }
      if (target < 0 || target >= count) { return std::nullopt; }
      return members[static_cast<std::size_t>(target)];
    };
    const auto same = [](const auto* got, const std::optional<Row>& want) {
      return got == nullptr ? !want.has_value() : want.has_value() && plain(*got) == *want;
    };
    std::vector<Row> probes = members;
    probes.emplace_back(kValues, kValues, kValues);  // never a member
    for (const Row& r : probes) {
      const A a = columnAt<A>(std::get<0>(r));
      const B b = columnAt<B>(std::get<1>(r));
      const C c = columnAt<C>(std::get<2>(r));
      for (std::ptrdiff_t steps = -3; steps <= 3; ++steps) {
        DREAMOR_CHECK(same(O.next(a, b, c, steps), expected(r, steps, false)));
        DREAMOR_CHECK(same(O.prev(a, b, c, steps), expected(r, -steps, false)));
        DREAMOR_CHECK(same(O.nextCyclic(a, b, c, steps), expected(r, steps, true)));
        DREAMOR_CHECK(same(O.prevCyclic(a, b, c, steps), expected(r, -steps, true)));
      }
    }

    // orderBy(): a stable sort by the key, ascending or descending. The key reads two columns, out of
    // their declared order, and compares by index - the values' own order differs by layout. Five
    // values a column leave many rows with one key, so stability shows both ways.
    const auto key_of   = [](B b, C c) { return std::tuple{indexOf(c), indexOf(b)}; };
    const auto plainKey = [](const Row& r) { return std::tuple{std::get<2>(r), std::get<1>(r)}; };
    std::vector<Row> up = s;
    std::ranges::stable_sort(up, std::ranges::less{}, plainKey);
    std::vector<Row> down = s;
    std::ranges::stable_sort(down, std::ranges::greater{}, plainKey);
    DREAMOR_CHECK(rowsOf(S.orderBy(key_of)) == up);
    DREAMOR_CHECK(rowsOf(S.orderBy(key_of, dreamor::ascending)) == up);
    DREAMOR_CHECK(rowsOf(S.orderBy(key_of, dreamor::descending)) == down);

    // sumOf: one term per row, narrowed by where(), one sum per split() predicate - a row may land
    // in both.
    const dreamor::concert::VarTable<A, B, C> x{env, S, 0, 1, ILOFLOAT, "x"};
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
    IloExpr all = dreamor::concert::sumOf(env, S, x).build();
    DREAMOR_CHECK(terms(all) == s.size());
    all.end();
    auto split = dreamor::concert::sumOf(env, S, x)
                     .where([&keep](A a, B b) { return keep.keeps(indexOf(a), indexOf(b)); })
                     .split([&first](B b, C c) { return first.keeps(indexOf(b), indexOf(c)); },
                            [&second](A a, C c) { return second.keeps(indexOf(a), indexOf(c)); })
                     .build();
    DREAMOR_CHECK(terms(split[0]) == in_first && terms(split[1]) == in_second && in_first <= kept);
    for (auto& sum : split) {
      sum.end();
    }

    // Lookups against each row's position in S, for x and a second table y on S - the two share
    // S's row index - with a table z on another set's rows in between, leaving its own rows as the
    // last found, and after each a lookup in a table w on the join's rows, another Key, so that the
    // thread's hints hold rows of both. Asked in a fixed order but for the sweep over every key,
    // whose order takes one draw, so no draw shifts the cases after this one however many lookups
    // there are: each row of S twice (contains, then the variable), its successor and its
    // predecessor; every possible key, absent ones included, in a random order; each row by
    // reference; and by columns inside a sum, where the sum's row answers.
    const dreamor::concert::VarTable<A, B, C> y{env, S, 0, 1, ILOFLOAT, "y"};
    const dreamor::concert::VarTable<A, B, C> z{env, T, 0, 1, ILOFLOAT, "z"};
    const auto W = SU.template select<A, B, C, D>();
    const dreamor::concert::VarTable<A, B, C, D> w{env, W, 0, 1, ILOFLOAT, "w"};
    std::map<Row, std::size_t> position;
    for (std::size_t i = 0; i < s.size(); ++i) {
      position.emplace(s[i], i);
    }
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
    for (const auto& row : W) {
      const JoinRow r = plainJoin(row);
      w_position.emplace(r, w_position.size());
      w_probes.push_back(r);
      w_probes.emplace_back(std::get<0>(r), std::get<1>(r), std::get<2>(r), kValues);
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
    std::vector<Row> every_key;  // a == kValues: never in a set
    every_key.reserve((kValues + 1) * kValues * kValues);
    for (std::size_t a = 0; a <= kValues; ++a) {
      for (std::size_t b = 0; b < kValues; ++b) {
        for (std::size_t c = 0; c < kValues; ++c) {
          every_key.emplace_back(a, b, c);
        }
      }
    }
    std::mt19937 sweep{g()};
    std::ranges::shuffle(every_key, sweep);
    for (const Row& r : every_key) {
      lookups = lookups && finds(r) && findsInW();
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
      const auto& row = *(S.begin() + static_cast<std::ptrdiff_t>(i));
      lookups         = lookups && x(row).getImpl() == x_vars[i] && y(row).getImpl() == y_vars[i];
    }
    for (std::size_t i = 0; i < W.size(); ++i) {
      lookups = lookups && w(*(W.begin() + static_cast<std::ptrdiff_t>(i))).getImpl() == w_vars[i];
    }
    DREAMOR_CHECK(lookups);
    IloExpr twice = dreamor::concert::sumOf(env, S, [&y](A a, B b, C c) { return 2 * y(a, b, c); }).build();
    bool in_order = terms(twice) == y_vars.size();
    for (std::size_t k = 0; in_order && k < y_vars.size(); ++k) {
      in_order = twice.expr_->terms[k].first == y_vars[k] && exactlyEqual(twice.expr_->terms[k].second, 2);
    }
    DREAMOR_CHECK(in_order);
    twice.end();
  }
};

// The whole test on Layout's columns, reported under name.
template <typename Layout>
[[nodiscard]] int run(const char* name) {
  const std::optional<std::uint32_t> seed_set = fromEnvironment<std::uint32_t>("DREAMOR_DIFF_SEED");
  const std::optional<std::size_t> cases_set  = fromEnvironment<std::size_t>("DREAMOR_DIFF_CASES");
  const std::uint32_t seed                    = seed_set.value_or(kSeed);
  const std::size_t cases                     = cases_set.value_or(kCases);
  if (seed_set) { std::printf("  %s: seed %u, from DREAMOR_DIFF_SEED\n", name, seed); }
  if (cases_set) { std::printf("  %s: %zu cases, from DREAMOR_DIFF_CASES\n", name, cases); }
  std::mt19937 g(seed);  // NOLINT(bugprone-random-generator-seed): fixed on purpose, every run checks the same cases
  IloEnv env;
  for (std::size_t i = 0; i < cases; ++i) {
    const std::size_t failures_before = g_failures;
    Differential<Layout>::check(g, env);
    if (g_failures != failures_before) { std::printf("  %s: case %zu of seed %u failed\n", name, i, seed); }
  }
  using Key  = std::tuple<typename Layout::A, typename Layout::B, typename Layout::C>;
  using Join = std::tuple<typename Layout::A, typename Layout::B, typename Layout::C, typename Layout::D>;
  std::printf(
      "%s: seed %u, %zu cases, rows of %zu bytes, joined rows of %zu\n", name, seed, cases, sizeof(Key), sizeof(Join));
  env.end();
  return ::report(name);
}

}  // namespace differential
