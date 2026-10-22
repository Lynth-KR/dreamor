// SPDX-License-Identifier: Apache-2.0
// Differential test: the set layer and sumOf against a reference written with nothing but the
// standard library - nested loops, std::set, std::map - over a few hundred small random cases
// from a fixed seed. The cases lean on the edges: empty sources, one element, many repeats,
// predicates that keep everything or nothing.
//
// makeSet leaves unspecified which column varies fastest, so its rows are compared as a
// multiset. Everything downstream is specified to the row (select / unite / intersect /
// difference keep first occurrences, concat keeps all, groups come in first-appearance order,
// ordered() is the deduplicated set order), so the reference runs on the rows dreamor actually
// produced and the results are compared in order.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <tuple>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

using ACol = dreamor::Col<std::size_t, "a">;
using BCol = dreamor::Col<std::size_t, "b">;
using CCol = dreamor::Col<std::size_t, "c">;
using Row  = std::tuple<std::size_t, std::size_t, std::size_t>;

namespace {

constexpr std::uint32_t kSeed      = 20261021;
constexpr std::size_t kCases       = 400;
constexpr std::size_t kValues      = 5;  // every column takes values 0..kValues-1
constexpr std::size_t kMaxListSize = 6;

template <typename Tup>
Row plain(const Tup& row) {
  return {std::get<ACol>(row).get(), std::get<BCol>(row).get(), std::get<CCol>(row).get()};
}

template <typename Set>
std::vector<Row> rowsOf(const Set& set) {
  std::vector<Row> out;
  for (const auto& row : set) out.push_back(plain(row));
  return out;
}

template <typename T>
std::vector<T> firstOccurrences(const std::vector<T>& rows) {
  std::set<T> seen;
  std::vector<T> out;
  for (const auto& row : rows)
    if (seen.insert(row).second) out.push_back(row);
  return out;
}

std::vector<Row> sorted(std::vector<Row> rows) {
  std::ranges::sort(rows);
  return rows;
}

// A list of values, sized to hit the edges often: empty and single lists, then longer ones
// whose small value range makes repeats likely.
std::vector<std::size_t> randomList(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> shape(0, 9);
  std::uniform_int_distribution<std::size_t> longer(2, kMaxListSize);
  std::uniform_int_distribution<std::size_t> value(0, kValues - 1);
  const std::size_t pick = shape(g);
  std::size_t size       = pick;  // 0 and 1 as they come
  if (pick >= 2) size = longer(g);
  std::vector<std::size_t> out(size);
  for (auto& v : out) v = value(g);
  return out;
}

// (x + y) % modulus != residue - or a predicate that keeps everything (modulus 1, residue 1)
// or nothing (modulus 1, residue 0).
struct Rule {
  std::size_t modulus, residue;
  [[nodiscard]] bool keeps(std::size_t x, std::size_t y) const { return (x + y) % modulus != residue; }
};
Rule randomRule(std::mt19937& g) {
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
Data randomData(std::mt19937& g) {
  Data d{randomList(g), randomList(g), {}, randomRule(g), randomRule(g)};
  for (std::size_t a = 0; a < kValues; ++a) d.cs_of.push_back(randomList(g));
  return d;
}

// The reference makeSet: nested loops in declaration order, predicates applied to full rows.
std::vector<Row> referenceRows(const Data& d) {
  std::vector<Row> out;
  for (const std::size_t a : d.as)
    for (const std::size_t b : d.bs)
      for (const std::size_t c : d.cs_of[a])
        if (d.ac.keeps(a, c) && d.b_only.keeps(b, 0)) out.emplace_back(a, b, c);
  return out;
}

auto buildSet(const Data& d) {
  const auto cs  = [&d](ACol a) { return std::span<const std::size_t>(d.cs_of[a.get()]); };
  const auto ac  = [&d](ACol a, CCol c) { return d.ac.keeps(a.get(), c.get()); };
  const auto bok = [&d](BCol b) { return d.b_only.keeps(b.get(), 0); };
  return dreamor::makeSet<ACol, BCol, CCol>(d.as, d.bs, cs).where(ac).where(bok).collect();
}

std::size_t terms(const IloExpr& e) {
  return e.expr_->terms.size();
}

void checkCase(std::mt19937& g, IloEnv env) {
  const Data d             = randomData(g);
  const Data e             = randomData(g);
  const auto S             = buildSet(d);
  const auto T             = buildSet(e);
  const std::vector<Row> s = rowsOf(S);
  const std::vector<Row> t = rowsOf(T);

  // makeSet with a fixed, a fixed and a dependent source, filtered while it generates.
  DREAMOR_CHECK(sorted(s) == sorted(referenceRows(d)));
  DREAMOR_CHECK(sorted(t) == sorted(referenceRows(e)));

  // where() on a finished set keeps the order.
  const Rule rule = randomRule(g);
  std::vector<Row> filtered;
  for (const Row& r : s)
    if (rule.keeps(std::get<1>(r), std::get<2>(r))) filtered.push_back(r);
  DREAMOR_CHECK(rowsOf(S.where([&rule](BCol b, CCol c) { return rule.keeps(b.get(), c.get()); })) == filtered);

  // select(): projection, first occurrences.
  std::vector<std::pair<std::size_t, std::size_t>> ac;
  ac.reserve(s.size());
  for (const Row& r : s) ac.emplace_back(std::get<0>(r), std::get<2>(r));
  std::vector<std::pair<std::size_t, std::size_t>> selected;
  for (const auto& row : S.select<ACol, CCol>())
    selected.emplace_back(std::get<ACol>(row).get(), std::get<CCol>(row).get());
  DREAMOR_CHECK(selected == firstOccurrences(ac));

  // select(expr): each row as the expression makes it - here a and c trading columns.
  std::vector<std::pair<std::size_t, std::size_t>> ca;
  ca.reserve(ac.size());
  for (const auto& [a, c] : ac) ca.emplace_back(c, a);
  std::vector<std::pair<std::size_t, std::size_t>> traded;
  for (const auto& row : S.select([](ACol a, CCol c) { return std::tuple<ACol, CCol>{c, a}; }))
    traded.emplace_back(std::get<ACol>(row).get(), std::get<CCol>(row).get());
  DREAMOR_CHECK(traded == firstOccurrences(ca));

  // The algebra, in order.
  std::vector<Row> both = s;
  both.insert(both.end(), t.begin(), t.end());
  const std::set<Row> s_set(s.begin(), s.end());
  const std::set<Row> t_set(t.begin(), t.end());
  std::vector<Row> in_both;
  std::vector<Row> only_s;
  for (const Row& r : firstOccurrences(s)) (t_set.contains(r) ? in_both : only_s).push_back(r);
  DREAMOR_CHECK(rowsOf(dreamor::concat(S, T)) == both);
  DREAMOR_CHECK(rowsOf(dreamor::unite(S, T)) == firstOccurrences(both));
  DREAMOR_CHECK(rowsOf(dreamor::intersect(S, T)) == in_both);
  DREAMOR_CHECK(rowsOf(dreamor::difference(S, T)) == only_s);
  DREAMOR_CHECK(rowsOf(S.distinct()) == firstOccurrences(s));

  // membership() over every possible row.
  const auto inS = S.membership();
  for (std::size_t a = 0; a < kValues; ++a)
    for (std::size_t b = 0; b < kValues; ++b)
      for (std::size_t c = 0; c < kValues; ++c)
        DREAMOR_CHECK(inS(ACol{a}, BCol{b}, CCol{c}) == s_set.contains(Row{a, b, c}));

  // groupBy(): groups by first appearance, rows in set order; slice() for every key, present or not.
  std::vector<std::size_t> key_order;
  std::map<std::size_t, std::vector<Row>> groups;
  for (const Row& r : s) {
    if (!groups.contains(std::get<0>(r))) key_order.push_back(std::get<0>(r));
    groups[std::get<0>(r)].push_back(r);
  }
  const auto byA = S.groupBy<ACol>();
  DREAMOR_CHECK(byA.size() == key_order.size());
  for (std::size_t i = 0; i < std::min(byA.size(), key_order.size()); ++i) {
    DREAMOR_CHECK(std::get<ACol>(byA.keyAt(i)).get() == key_order[i]);
    DREAMOR_CHECK(rowsOf(byA.at(i)) == groups[key_order[i]]);
  }
  for (std::size_t a = 0; a < kValues; ++a) {
    const auto expected = groups.contains(a) ? groups[a] : std::vector<Row>{};
    DREAMOR_CHECK(rowsOf(byA.slice(ACol{a})) == expected);
    DREAMOR_CHECK(byA.contains(ACol{a}) == groups.contains(a) && byA.sliceSize(ACol{a}) == expected.size());
  }
  std::map<std::pair<std::size_t, std::size_t>, std::vector<Row>> by_bc;
  for (const Row& r : s) by_bc[{std::get<1>(r), std::get<2>(r)}].push_back(r);
  const auto byBC = S.groupBy<BCol, CCol>();
  DREAMOR_CHECK(byBC.size() == by_bc.size());
  for (const auto& [bc, rows] : by_bc) DREAMOR_CHECK(rowsOf(byBC.slice(BCol{bc.first}, CCol{bc.second})) == rows);

  // ordered(): the deduplicated set order; next / prev / cyclic with steps, nullptr off the
  // ends and for a non-member.
  const std::vector<Row> members = firstOccurrences(s);
  const auto O                   = S.ordered();
  DREAMOR_CHECK(rowsOf(O.members()) == members);
  const auto count    = static_cast<std::ptrdiff_t>(members.size());
  const auto expected = [&](const Row& r, std::ptrdiff_t steps, bool cyclic) -> std::optional<Row> {
    const auto at = std::ranges::find(members, r);
    if (at == members.end()) return std::nullopt;
    std::ptrdiff_t target = (at - members.begin()) + steps;
    if (cyclic) target = ((target % count) + count) % count;
    if (target < 0 || target >= count) return std::nullopt;
    return members[static_cast<std::size_t>(target)];
  };
  const auto same = [](const auto* got, const std::optional<Row>& want) {
    return got == nullptr ? !want.has_value() : want.has_value() && plain(*got) == *want;
  };
  std::vector<Row> probes = members;
  probes.emplace_back(kValues, kValues, kValues);  // never a member
  for (const Row& r : probes) {
    const ACol a{std::get<0>(r)};
    const BCol b{std::get<1>(r)};
    const CCol c{std::get<2>(r)};
    for (std::ptrdiff_t steps = -3; steps <= 3; ++steps) {
      DREAMOR_CHECK(same(O.next(a, b, c, steps), expected(r, steps, false)));
      DREAMOR_CHECK(same(O.prev(a, b, c, steps), expected(r, -steps, false)));
      DREAMOR_CHECK(same(O.nextCyclic(a, b, c, steps), expected(r, steps, true)));
      DREAMOR_CHECK(same(O.prevCyclic(a, b, c, steps), expected(r, -steps, true)));
    }
  }

  // sumOf: one term per row (repeats included), narrowed by where(), one sum per split()
  // predicate - a row may land in both.
  const auto D = S.distinct();
  const dreamor::concert::VarTable<ACol, BCol, CCol> x{env, D, 0, 1, ILOFLOAT, "x"};
  const Rule keep       = randomRule(g);
  const Rule first      = randomRule(g);
  const Rule second     = randomRule(g);
  std::size_t kept      = 0;
  std::size_t in_first  = 0;
  std::size_t in_second = 0;
  for (const Row& r : s) {
    if (!keep.keeps(std::get<0>(r), std::get<1>(r))) continue;
    ++kept;
    if (first.keeps(std::get<1>(r), std::get<2>(r))) ++in_first;
    if (second.keeps(std::get<0>(r), std::get<2>(r))) ++in_second;
  }
  IloExpr all = dreamor::concert::sumOf(env, S, x).build();
  DREAMOR_CHECK(terms(all) == s.size());
  all.end();
  auto split = dreamor::concert::sumOf(env, S, x)
                   .where([&keep](ACol a, BCol b) { return keep.keeps(a.get(), b.get()); })
                   .split([&first](BCol b, CCol c) { return first.keeps(b.get(), c.get()); },
                          [&second](ACol a, CCol c) { return second.keeps(a.get(), c.get()); })
                   .build();
  DREAMOR_CHECK(terms(split[0]) == in_first && terms(split[1]) == in_second && in_first <= kept);
  for (auto& sum : split) sum.end();
}

}  // namespace

int main() try {
  std::mt19937 g(kSeed);  // NOLINT(bugprone-random-generator-seed): fixed on purpose, every run checks the same cases
  IloEnv env;
  for (std::size_t i = 0; i < kCases; ++i) {
    const std::size_t failures_before = g_failures;
    checkCase(g, env);
    if (g_failures != failures_before) std::printf("  differential: case %zu of seed %u failed\n", i, kSeed);
  }
  std::printf("differential: seed %u, %zu cases\n", kSeed, kCases);
  env.end();
  return ::report("differential");
} catch (...) {
  return ::uncaught();
}
