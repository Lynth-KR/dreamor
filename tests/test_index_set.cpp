// SPDX-License-Identifier: Apache-2.0
// IndexSet and its derivations: where, orderBy, distinct, select, groupBy, membership,
// ordered, assertUnique, and the binary operations.
#include <algorithm>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "dreamor/index_set.hpp"
using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;

template <typename S>
constexpr bool canPushBack = requires(S& s) { s.push_back(std::declval<typename S::value_type>()); };

int main() {
  const std::vector<std::size_t> I{3, 1, 2, 4}, J{10, 20, 30};
  const auto IJ   = dreamor::makeSet<ICol, JCol>(I, J).collect();
  const auto II   = dreamor::makeSet<ICol>(I).collect();
  const auto even = [](ICol i) { return i.get() % 2 == 0; };
  const auto big  = [](JCol j) { return j.get() >= 20; };

  // read-only, copy shares rows
  static_assert(!canPushBack<dreamor::SetOf<ICol>>);
  static_assert(std::ranges::random_access_range<dreamor::SetOf<ICol, JCol>> &&
                std::ranges::sized_range<dreamor::SetOf<ICol>>);
  const auto copy = IJ;
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

  // distinct: the same set back when nothing repeats
  DREAMOR_CHECK(II.distinct().begin() == II.begin());
  DREAMOR_CHECK(dreamor::concat(II, II).distinct().size() == 4);

  // select: projection as a set, and column reorder
  DREAMOR_CHECK(IJ.select<JCol>().size() == 3);
  const auto JI = IJ.select<JCol, ICol>();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(JI)>, std::tuple<JCol, ICol>>);
  DREAMOR_CHECK(JI.size() == 12 && std::get<JCol>(*JI.begin()).get() == 10 && std::get<ICol>(*JI.begin()).get() == 3);

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
  std::size_t pairs = 0, groups = 0;
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
  const auto T = II.ordered();
  DREAMOR_CHECK(T.prev(ICol{1}) != nullptr && std::get<ICol>(*T.prev(ICol{1})).get() == 3 &&
                T.next(ICol{4}) == nullptr);

  // assertUnique: the set back, or a throw (debug builds)
  DREAMOR_CHECK(IJ.assertUnique().begin() == IJ.begin());
#ifndef NDEBUG
  DREAMOR_CHECK_THROWS(std::logic_error, dreamor::concat(II, II).assertUnique());
  DREAMOR_CHECK_THROWS(std::logic_error, IJ.assertUnique<ICol>());
#endif

  // binary operations
  DREAMOR_CHECK(dreamor::unite(II, II).size() == 4);
  DREAMOR_CHECK(dreamor::intersect(IJ, IJ.where(even)).size() == 6);
  DREAMOR_CHECK(dreamor::difference(IJ, IJ.where(even)).size() == 6);
  DREAMOR_CHECK(dreamor::concat(II, II).size() == 8);

  // a set spread into a new one
  const auto IJK = dreamor::makeSet<>(IJ).collect();
  DREAMOR_CHECK(std::ranges::equal(IJK, IJ));
  return report("index_set");
}
