// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// IndexSet and its derivations: where, orderBy, select, groupBy, membership, ordered, and the
// binary operations.
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/index_set.hpp"
using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;
using cK = dreamor::Col<std::size_t, "k">;
using cL = dreamor::Col<std::size_t, "l">;

template <typename S>
constexpr bool canPushBack = requires(S& s) { s.push_back(std::declval<typename S::value_type>()); };

int main() try {
  const std::vector<std::size_t> I{3, 1, 2, 4};
  const std::vector<std::size_t> J{10, 20, 30};
  const auto IJ   = dreamor::makeSet<cI, cJ>(I, J).collect();
  const auto II   = dreamor::makeSet<cI>(I).collect();
  const auto even = [](cI i) { return i.get() % 2 == 0; };
  const auto big  = [](cJ j) { return j.get() >= 20; };

  // read-only, copy shares rows
  static_assert(!canPushBack<dreamor::IndexSet<cI>>);
  static_assert(std::ranges::random_access_range<dreamor::IndexSet<cI, cJ>> &&
                std::ranges::sized_range<dreamor::IndexSet<cI>>);
  const auto copy = IJ;  // NOLINT(performance-unnecessary-copy-initialization): the copy is what is checked
  DREAMOR_CHECK(copy.begin() == IJ.begin() && IJ.size() == 12);

  // where: AND of predicates, source untouched, order preserved
  DREAMOR_CHECK(IJ.where(even).size() == 6);
  DREAMOR_CHECK(IJ.where(even, big).size() == 4);
  DREAMOR_CHECK(
      std::ranges::equal(IJ.where(even, big), dreamor::makeSet<cI, cJ>(I, J).where(even).where(big).collect()));
  DREAMOR_CHECK(IJ.size() == 12);

  // orderBy: stable, the key on the columns it reads; ascending by default, or descending
  const auto i_of   = [](cI i) { return i.get(); };
  const auto sorted = II.orderBy(i_of);
  DREAMOR_CHECK(std::get<cI>(*sorted.begin()).get() == 1 && std::get<cI>(*II.begin()).get() == 3);
  DREAMOR_CHECK(std::ranges::equal(II.orderBy(i_of, dreamor::ascending), sorted));
  const auto down = II.orderBy(i_of, dreamor::descending);
  DREAMOR_CHECK(std::get<cI>(*down.begin()).get() == 4 && std::get<cI>(*std::prev(down.end())).get() == 1);
  // stable both ways: rows with one key keep the set's order (each i's j stay 10, 20, 30)
  const auto byJDown = IJ.orderBy([](cJ j) { return j.get(); }, dreamor::descending);
  DREAMOR_CHECK(*byJDown.begin() == std::tuple{cI{3}, cJ{30}} &&
                *std::next(byJDown.begin()) == std::tuple{cI{1}, cJ{30}});
  const auto byIJ = IJ.orderBy([](cI i, cJ j) { return std::tuple{i.get(), j.get()}; });  // a key over two columns
  DREAMOR_CHECK(*byIJ.begin() == std::tuple{cI{1}, cJ{10}} && *std::prev(byIJ.end()) == std::tuple{cI{4}, cJ{30}});

  // select: projection as a set, and column reorder
  DREAMOR_CHECK(IJ.select<cJ>().size() == 3);
  const auto JI = IJ.select<cJ, cI>();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(JI)>, std::tuple<cJ, cI>>);
  DREAMOR_CHECK(JI.size() == 12 && std::get<cJ>(*JI.begin()).get() == 10 && std::get<cI>(*JI.begin()).get() == 3);

  // select(expr): each row as expr makes it, as a set; a value changes column only where the
  // new column is written out
  static_assert(!std::is_convertible_v<cJ, cI>);
  const auto swapped = IJ.select([](cI i, cJ j) { return std::tuple<cI, cJ>{j, i}; });
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(swapped)>, std::tuple<cI, cJ>>);
  DREAMOR_CHECK(swapped.size() == 12 && std::get<cI>(*swapped.begin()).get() == 10 &&
                std::get<cJ>(*swapped.begin()).get() == 3);
  DREAMOR_CHECK(std::ranges::equal(IJ.select([](cJ j) { return j; }), IJ.select<cJ>()));
  DREAMOR_CHECK(IJ.select([](cI i) { return cI{i.get() % 2}; }).size() == 2);

  // groupBy: groups in first-appearance order, rows in set order, random-access slices
  const auto byI = IJ.groupBy<cI>();
  DREAMOR_CHECK(byI.size() == 4 && std::get<cI>(byI.keyAt(0)).get() == 3);
  const auto s1 = byI.slice(cI{1});
  static_assert(std::ranges::random_access_range<decltype(s1)> && std::ranges::borrowed_range<decltype(s1)>);
  DREAMOR_CHECK(s1.size() == 3 && !s1.empty());
  DREAMOR_CHECK(std::get<cJ>(s1[0]).get() == 10 && std::get<cJ>(s1.back()).get() == 30 &&
                std::get<cJ>(s1.front()).get() == 10);
  DREAMOR_CHECK((s1.end() - s1.begin()) == 3 && std::get<cJ>(*(s1.begin() + 2)).get() == 30);
  DREAMOR_CHECK(byI.slice(cI{99}).empty() && !byI.contains(cI{99}) && byI.sliceSize(cI{2}) == 3);
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
  for (const auto& [i, j, rows] : IJ.groupBy<cI, cJ>()) {
    DREAMOR_CHECK(rows.size() == 1 && std::get<cI>(rows.front()) == i && std::get<cJ>(rows.front()) == j);
    ++pairs;
  }
  for (const auto& group : IJ.groupBy<cI>()) {
    DREAMOR_CHECK(std::get<cI>(group.key) == std::get<cI>(group.tuples.front()));
    ++groups;
  }
  DREAMOR_CHECK(pairs == 12 && groups == 4);
  DREAMOR_CHECK(std::ranges::equal(byI.regrouped().select<cI>(), II));
  DREAMOR_CHECK_THROWS(std::out_of_range, byI.at(4));

  // membership: as a predicate in a builder where
  const auto inEven = IJ.where(even).membership();
  DREAMOR_CHECK(dreamor::makeSet<cI, cJ>(I, J).where(inEven).collect().size() == 6);

  // ordered
  const auto T          = II.ordered();
  const auto* beforeOne = T.prev(cI{1});
  DREAMOR_CHECK(beforeOne != nullptr && std::get<cI>(*beforeOne).get() == 3 && T.next(cI{4}) == nullptr);
  DREAMOR_CHECK(&T.prevOf(cI{1}) == beforeOne && &T.nextOf(cI{3}) == T.next(cI{3}));  // the row itself, where it is

  // ordered within groups - next(j, S[i]): each i's rows here are interleaved with the other's
  {
    const std::vector<std::size_t> Js{10, 20, 30};
    const std::vector<std::size_t> Is{1, 2};
    const auto rowsJI  = dreamor::makeSet<cJ, cI>(Js, Is).collect();  // (10,1) (10,2) (20,1) (20,2) ...
    const auto withinI = rowsJI.ordered<cI>();
    const auto* after  = withinI.next(cJ{10}, cI{1});
    const auto* around = withinI.nextCyclic(cJ{30}, cI{2});
    const auto* behind = withinI.prevCyclic(cJ{10}, cI{1});
    DREAMOR_CHECK(after != nullptr && *after == std::tuple{cJ{20}, cI{1}});
    DREAMOR_CHECK(withinI.next(cJ{30}, cI{1}) == nullptr && withinI.prev(cJ{10}, cI{2}) == nullptr);
    DREAMOR_CHECK(around != nullptr && *around == std::tuple{cJ{10}, cI{2}});
    DREAMOR_CHECK(behind != nullptr && *behind == std::tuple{cJ{30}, cI{1}});
    DREAMOR_CHECK(withinI.next(cJ{10}, cI{1}, 2) != nullptr && withinI.next(cJ{10}, cI{1}, 3) == nullptr);
    DREAMOR_CHECK(withinI.nextOf(cJ{10}, cI{1}) == *after &&
                  withinI.nextOf(cJ{10}, cI{1}, 2) == std::tuple{cJ{30}, cI{1}});
    DREAMOR_CHECK(withinI.prevOf(cJ{30}, cI{2}, 2) == std::tuple{cJ{10}, cI{2}} &&
                  withinI.prevOf(*after) == std::tuple{cJ{10}, cI{1}});
    DREAMOR_CHECK(withinI.ord(cJ{20}, cI{2}) == std::optional<std::size_t>{1} && withinI.isFirst(cJ{10}, cI{2}));
    DREAMOR_CHECK(withinI.isLast(cJ{30}, cI{1}) && !withinI.isLast(cJ{20}, cI{1}) && !withinI.isFirst(cJ{20}, cI{1}));
    DREAMOR_CHECK(withinI.next(cJ{40}, cI{1}) == nullptr && !withinI.contains(cJ{40}, cI{1}) &&
                  withinI.contains(*after));
    DREAMOR_CHECK(withinI.size() == 6 && withinI.members() == rowsJI);
    // first(S[i]) and last(S[i]): a group's ends, named by its key
    DREAMOR_CHECK(withinI.first(cI{1}) == std::tuple{cJ{10}, cI{1}} &&
                  withinI.last(cI{1}) == std::tuple{cJ{30}, cI{1}});
    DREAMOR_CHECK(withinI.first(cI{2}) == std::tuple{cJ{10}, cI{2}} &&
                  withinI.last(cI{2}) == std::tuple{cJ{30}, cI{2}});
    DREAMOR_CHECK(withinI.isFirst(withinI.first(cI{2})) && withinI.isLast(withinI.last(cI{1})));
    const auto alone = rowsJI.ordered<cJ, cI>();  // every row its own group
    DREAMOR_CHECK(alone.next(cJ{10}, cI{1}) == nullptr && alone.isFirst(cJ{10}, cI{1}));
  }

  // binary operations
  DREAMOR_CHECK(dreamor::unite(II, II).size() == 4);
  DREAMOR_CHECK(dreamor::intersect(IJ, IJ.where(even)).size() == 6);
  DREAMOR_CHECK(dreamor::difference(IJ, IJ.where(even)).size() == 6);

  // equality: the same rows, in any order; order is std::ranges::equal's business
  const auto reversed = IJ.orderBy(i_of, dreamor::descending);
  DREAMOR_CHECK(reversed == IJ && IJ == reversed && !std::ranges::equal(reversed, IJ));
  DREAMOR_CHECK(IJ == IJ && dreamor::unite(IJ.where(even), dreamor::difference(IJ, IJ.where(even))) == IJ);
  DREAMOR_CHECK(IJ != IJ.where(even) && IJ.where(even) != dreamor::difference(IJ, IJ.where(even)));
  DREAMOR_CHECK(IJ.where(even).size() == dreamor::difference(IJ, IJ.where(even)).size());  // same size, other rows
  DREAMOR_CHECK(IJ.where([](cI) { return false; }) == dreamor::IndexSet<cI, cJ>{});

  // the visit order: with no predicate to tell them apart, a dependent source goes before a
  // container - its list per binding is usually the shorter - and containers keep declaration order
  {
    const std::vector<std::size_t> As{1, 2};
    const std::vector<std::size_t> Bs{3, 4};
    const std::vector<std::size_t> Cs{5, 6};
    const auto c_of = [&Cs](cI) -> const std::vector<std::size_t>& { return Cs; };
    using Planned   = decltype(dreamor::makeSet<cI, cJ, cK>(As, Bs, c_of));
    static_assert(Planned::kVisitOrder == std::array<std::size_t, 3>{0, 2, 1});
    DREAMOR_CHECK(dreamor::makeSet<cI, cJ, cK>(As, Bs, c_of).collect().size() == 8);
  }

  // a set spread into a new one
  const auto IJK = dreamor::makeSet<>(IJ).collect();
  DREAMOR_CHECK(std::ranges::equal(IJK, IJ));

  // two sets sharing a column: the natural join on it - the rows of the product that agree
  const std::vector<std::size_t> K{5, 6};
  const auto JK =
      dreamor::makeSet<cJ, cK>(J, K).where([](cJ j, cK k) { return (j.get() / 10) + k.get() != 8; }).collect();
  const auto joined = dreamor::makeSet<>(IJ, JK).collect();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(joined)>, std::tuple<cI, cJ, cK>>);
  DREAMOR_CHECK(JK.size() == 4 && joined.size() == 16);
  DREAMOR_CHECK(std::ranges::is_permutation(joined, dreamor::makeSet<cK>(IJ, K).where(JK.membership()).collect()));
  DREAMOR_CHECK(std::ranges::is_permutation(joined, dreamor::makeSet<>(JK, IJ).collect().select<cI, cJ, cK>()));
  DREAMOR_CHECK(dreamor::makeSet<>(IJ, JK).where(even).collect().size() == 8);
  // a chain of three, each joined on what the one before it bound
  const std::vector<std::size_t> L{7, 8};
  const auto KL    = dreamor::makeSet<cK, cL>(K, L).where([](cK k, cL l) { return k.get() + 2 == l.get(); }).collect();
  const auto chain = dreamor::makeSet<>(IJ, JK, KL).collect();
  DREAMOR_CHECK(chain.size() == 16 && std::ranges::all_of(chain, [](const auto& row) {
                  return std::get<cK>(row).get() + 2 == std::get<cL>(row).get();
                }));
  // a set inside another one's columns filters it, as where(membership()) does
  const auto II_even = II.where(even);
  DREAMOR_CHECK(std::ranges::equal(dreamor::makeSet<>(IJ, II_even).collect(), IJ.where(II_even.membership())));
  DREAMOR_CHECK(std::ranges::is_permutation(dreamor::makeSet<>(II_even, IJ).collect(), IJ.where(even)));
  // no match, no row
  const auto IJ_big = IJ.where(big);
  const auto JK_ten = JK.where([](cJ j) { return j.get() == 10; });
  DREAMOR_CHECK(dreamor::makeSet<>(IJ_big, JK_ten).collect().size() == 0);
  return report("index_set");
} catch (...) { return ::uncaught(); }
