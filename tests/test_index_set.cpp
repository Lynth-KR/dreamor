// SPDX-License-Identifier: Apache-2.0
// IndexSet and its derivations: where, orderBy, select, groupBy, membership, ordered, and the
// binary operations.
#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/index_set.hpp"
using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;
using KCol = dreamor::Col<std::size_t, "k">;
using LCol = dreamor::Col<std::size_t, "l">;

template <typename S>
constexpr bool canPushBack = requires(S& s) { s.push_back(std::declval<typename S::value_type>()); };

int main() try {
  const std::vector<std::size_t> I{3, 1, 2, 4};
  const std::vector<std::size_t> J{10, 20, 30};
  const auto IJ   = dreamor::makeSet<ICol, JCol>(I, J).collect();
  const auto II   = dreamor::makeSet<ICol>(I).collect();
  const auto even = [](ICol i) { return i.get() % 2 == 0; };
  const auto big  = [](JCol j) { return j.get() >= 20; };

  // read-only, copy shares rows
  static_assert(!canPushBack<dreamor::SetOf<ICol>>);
  static_assert(std::ranges::random_access_range<dreamor::SetOf<ICol, JCol>> &&
                std::ranges::sized_range<dreamor::SetOf<ICol>>);
  const auto copy = IJ;  // NOLINT(performance-unnecessary-copy-initialization): the copy is what is checked
  DREAMOR_CHECK(copy.begin() == IJ.begin() && IJ.size() == 12);

  // where: AND of predicates, source untouched, order preserved
  DREAMOR_CHECK(IJ.where(even).size() == 6);
  DREAMOR_CHECK(IJ.where(even, big).size() == 4);
  DREAMOR_CHECK(
      std::ranges::equal(IJ.where(even, big), dreamor::makeSet<ICol, JCol>(I, J).where(even).where(big).collect()));
  DREAMOR_CHECK(IJ.size() == 12);

  // orderBy: stable, key over the whole row
  const auto sorted = II.orderBy([](const auto& t) { return std::get<ICol>(t).get(); });
  DREAMOR_CHECK(std::get<ICol>(*sorted.begin()).get() == 1 && std::get<ICol>(*II.begin()).get() == 3);

  // select: projection as a set, and column reorder
  DREAMOR_CHECK(IJ.select<JCol>().size() == 3);
  const auto JI = IJ.select<JCol, ICol>();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(JI)>, std::tuple<JCol, ICol>>);
  DREAMOR_CHECK(JI.size() == 12 && std::get<JCol>(*JI.begin()).get() == 10 && std::get<ICol>(*JI.begin()).get() == 3);

  // select(expr): each row as expr makes it, as a set; a value changes column only where the
  // new column is written out
  static_assert(!std::is_convertible_v<JCol, ICol>);
  const auto swapped = IJ.select([](ICol i, JCol j) { return std::tuple<ICol, JCol>{j, i}; });
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(swapped)>, std::tuple<ICol, JCol>>);
  DREAMOR_CHECK(swapped.size() == 12 && std::get<ICol>(*swapped.begin()).get() == 10 &&
                std::get<JCol>(*swapped.begin()).get() == 3);
  DREAMOR_CHECK(std::ranges::equal(IJ.select([](JCol j) { return j; }), IJ.select<JCol>()));
  DREAMOR_CHECK(IJ.select([](ICol i) { return ICol{i.get() % 2}; }).size() == 2);

  // groupBy: groups in first-appearance order, rows in set order, random-access slices
  const auto byI = IJ.groupBy<ICol>();
  DREAMOR_CHECK(byI.size() == 4 && std::get<ICol>(byI.keyAt(0)).get() == 3);
  const auto s1 = byI.slice(ICol{1});
  static_assert(std::ranges::random_access_range<decltype(s1)> && std::ranges::borrowed_range<decltype(s1)>);
  DREAMOR_CHECK(s1.size() == 3 && !s1.empty());
  DREAMOR_CHECK(std::get<JCol>(s1[0]).get() == 10 && std::get<JCol>(s1.back()).get() == 30 &&
                std::get<JCol>(s1.front()).get() == 10);
  DREAMOR_CHECK((s1.end() - s1.begin()) == 3 && std::get<JCol>(*(s1.begin() + 2)).get() == 30);
  DREAMOR_CHECK(byI.slice(ICol{99}).empty() && !byI.contains(ICol{99}) && byI.sliceSize(ICol{2}) == 3);
  std::size_t visited = 0;
  for (const auto& [groupI, rows] : byI) {
    for (const auto& [i, j] : rows) {
      DREAMOR_CHECK(i.get() == groupI.get());
      ++visited;
    }
  }
  DREAMOR_CHECK(visited == 12);

  // groups destructure flat - key columns, then rows - and a temporary Grouped can be walked
  static_assert(std::tuple_size_v<std::ranges::range_value_t<decltype(byI)>> == 2);
  std::size_t pairs  = 0;
  std::size_t groups = 0;
  for (const auto& [i, j, rows] : IJ.groupBy<ICol, JCol>()) {
    DREAMOR_CHECK(rows.size() == 1 && std::get<ICol>(rows.front()) == i && std::get<JCol>(rows.front()) == j);
    ++pairs;
  }
  for (const auto& group : IJ.groupBy<ICol>()) {
    DREAMOR_CHECK(std::get<ICol>(group.key) == std::get<ICol>(group.tuples.front()));
    ++groups;
  }
  DREAMOR_CHECK(pairs == 12 && groups == 4);
  DREAMOR_CHECK(std::ranges::equal(byI.regrouped().select<ICol>(), II));
  DREAMOR_CHECK_THROWS(std::out_of_range, byI.at(4));

  // membership: as a predicate in a builder where
  const auto inEven = IJ.where(even).membership();
  DREAMOR_CHECK(dreamor::makeSet<ICol, JCol>(I, J).where(inEven).collect().size() == 6);

  // ordered
  const auto T          = II.ordered();
  const auto* beforeOne = T.prev(ICol{1});
  DREAMOR_CHECK(beforeOne != nullptr && std::get<ICol>(*beforeOne).get() == 3 && T.next(ICol{4}) == nullptr);

  // binary operations
  DREAMOR_CHECK(dreamor::unite(II, II).size() == 4);
  DREAMOR_CHECK(dreamor::intersect(IJ, IJ.where(even)).size() == 6);
  DREAMOR_CHECK(dreamor::difference(IJ, IJ.where(even)).size() == 6);

  // the visit order: with no predicate to tell them apart, a dependent source goes before a
  // container - its list per binding is usually the shorter - and containers keep declaration order
  {
    const std::vector<std::size_t> As{1, 2};
    const std::vector<std::size_t> Bs{3, 4};
    const std::vector<std::size_t> Cs{5, 6};
    const auto c_of = [&Cs](ICol) -> const std::vector<std::size_t>& { return Cs; };
    using Planned   = decltype(dreamor::makeSet<ICol, JCol, KCol>(As, Bs, c_of));
    static_assert(Planned::kVisitOrder == std::array<std::size_t, 3>{0, 2, 1});
    DREAMOR_CHECK(dreamor::makeSet<ICol, JCol, KCol>(As, Bs, c_of).collect().size() == 8);
  }

  // a set spread into a new one
  const auto IJK = dreamor::makeSet<>(IJ).collect();
  DREAMOR_CHECK(std::ranges::equal(IJK, IJ));

  // two sets sharing a column: the natural join on it - the rows of the product that agree
  const std::vector<std::size_t> K{5, 6};
  const auto JK =
      dreamor::makeSet<JCol, KCol>(J, K).where([](JCol j, KCol k) { return (j.get() / 10) + k.get() != 8; }).collect();
  const auto joined = dreamor::makeSet<>(IJ, JK).collect();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(joined)>, std::tuple<ICol, JCol, KCol>>);
  DREAMOR_CHECK(JK.size() == 4 && joined.size() == 16);
  DREAMOR_CHECK(std::ranges::is_permutation(joined, dreamor::makeSet<KCol>(IJ, K).where(JK.membership()).collect()));
  DREAMOR_CHECK(std::ranges::is_permutation(joined, dreamor::makeSet<>(JK, IJ).collect().select<ICol, JCol, KCol>()));
  DREAMOR_CHECK(dreamor::makeSet<>(IJ, JK).where(even).collect().size() == 8);
  // a chain of three, each joined on what the one before it bound
  const std::vector<std::size_t> L{7, 8};
  const auto KL =
      dreamor::makeSet<KCol, LCol>(K, L).where([](KCol k, LCol l) { return k.get() + 2 == l.get(); }).collect();
  const auto chain = dreamor::makeSet<>(IJ, JK, KL).collect();
  DREAMOR_CHECK(chain.size() == 16 && std::ranges::all_of(chain, [](const auto& row) {
                  return std::get<KCol>(row).get() + 2 == std::get<LCol>(row).get();
                }));
  // a set inside another one's columns filters it, as where(membership()) does
  const auto II_even = II.where(even);
  DREAMOR_CHECK(std::ranges::equal(dreamor::makeSet<>(IJ, II_even).collect(), IJ.where(II_even.membership())));
  DREAMOR_CHECK(std::ranges::is_permutation(dreamor::makeSet<>(II_even, IJ).collect(), IJ.where(even)));
  // no match, no row
  const auto IJ_big = IJ.where(big);
  const auto JK_ten = JK.where([](JCol j) { return j.get() == 10; });
  DREAMOR_CHECK(dreamor::makeSet<>(IJ_big, JK_ten).collect().size() == 0);
  return report("index_set");
} catch (...) { return ::uncaught(); }
