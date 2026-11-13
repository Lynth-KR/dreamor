// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// IndexSet and its derivations: where, orderBy, select, groupBy, membership, ordered, and the
// binary operations; the empty set none() returns; a slice's front() and back() read from a temporary slice inside a
// callable; and a set, a Grouped and an Ordered that are moved from, which are empty.
#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);
DREAMOR_VALUE_COLUMN(cK, std::size_t);
DREAMOR_VALUE_COLUMN(cL, std::size_t);
}  // namespace

namespace {
struct Node {
  std::size_t id;

  [[nodiscard]] std::size_t primaryKey() const { return id; }
};
}  // namespace

namespace {
DREAMOR_ENTITY_COLUMN(cNode, Node);
DREAMOR_VALUE_COLUMN(cName, std::string_view);
DREAMOR_VALUE_COLUMN(cText, std::string);  // holds its text
}  // namespace

namespace {
struct Link {
  std::size_t from;
  std::size_t to;
};

// A set as a member, built in the initializer list from the members declared before it.
class Network {
 public:
  explicit Network(std::vector<Link> links) : links_{std::move(links)}, sLINKS_{links_, kLinkOf} {}

  [[nodiscard]] const dreamor::IndexSet<cI, cJ>& links() const { return sLINKS_; }

 private:
  static constexpr auto kLinkOf = [](const Link& link) { return std::tuple<cI, cJ>{link.from, link.to}; };
  std::vector<Link> links_;
  dreamor::IndexSet<cI, cJ> sLINKS_;
};
}  // namespace

template <typename A, typename B>
constexpr bool canCompare = requires(const A& a, const B& b) { a == b; };

namespace {
DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);
DREAMOR_VALUE_COLUMN(cLater, std::uint32_t);
DREAMOR_ENTITY_COLUMN(cTo, Node);

// A value in a key column's place where its type names that column alone, and a row of one column, in every
// lookup of the set layer: contains, membership, slice, an Ordered's walks. A value its column's type cannot
// hold is in no set.
// IndexSet{records, row_of}: a value names its column by type, as in a key
void rowOfValuesByType() {
  dreamor::EntityVector<Node> nodes;
  static_cast<void>(nodes.emplace(Node{1}));
  nodes.freeze();
  const std::vector<Link> links{{3, 10}, {1, 20}, {3, 30}};
  // an integer of another type fills the set's one column of integers, beside a column by name
  const std::vector<int> starts{2, 5};
  const auto start_of = [&nodes](int start) { return std::tuple{cNode{nodes.at(1)}, start}; };
  const dreamor::IndexSet<cNode, cPeriod> sSTARTS{starts, start_of};
  DREAMOR_CHECK(sSTARTS.size() == 2 && std::get<cPeriod>(sSTARTS.begin()[1]) == 5 &&
                std::get<cNode>(*sSTARTS.begin()) == nodes.at(1));
  // text made in the call fills a column of std::string, which holds it
  const auto code_of = [](const Link& link) { return std::tuple{"L" + std::to_string(link.to), link.to}; };
  const dreamor::IndexSet<cJ, cText> sCODES{links, code_of};
  DREAMOR_CHECK(sCODES.size() == 3 && sCODES.contains(10, "L10") && std::get<cText>(sCODES.begin()[2]) == "L30");
  // text fills the set's one column of text
  const auto line_of = [](const Link& link) { return std::tuple{"line", link.to}; };
  const dreamor::IndexSet<cI, cName> sLINES{links, line_of};
  DREAMOR_CHECK(sLINES.size() == 3 && std::get<cName>(*sLINES.begin()) == "line" &&
                std::get<cI>(*sLINES.begin()) == 10);
}

void valuesInKeyPlaces() {
  const std::vector<std::size_t> items{1, 2};
  const std::vector<std::uint32_t> periods{1, 2, 3};
  const auto sIP      = dreamor::index_set::relational::from<cI>(items).cross<cPeriod>(periods).build();
  const auto sPERIODS = dreamor::index_set::relational::from<cPeriod>(periods).build();

  // IndexSet::contains: whether (i, t) is a row of sIP
  DREAMOR_CHECK(sIP.contains(cI{1}, cPeriod{2}) && sIP.contains(1, 2) && sIP.contains(*sIP.begin()));
  DREAMOR_CHECK(sIP.contains(std::size_t{2}, 3U) && sIP.contains(2, cLater{3}));
  DREAMOR_CHECK(!sIP.contains(1, 4) && !sIP.contains(3, 1) && !sIP.contains(1, -1) && !sIP.contains(-1, 1));
  DREAMOR_CHECK(!sIP.contains(1, std::uint64_t{1} << 32U));
  DREAMOR_CHECK(sPERIODS.contains(3) && !sPERIODS.contains(0) &&
                !sIP.where([](cPeriod t) { return t > 5; }).contains(1, 1));
  const auto IJ = dreamor::index_set::relational::from<cI>(items).cross<cJ>(items).build();
  DREAMOR_CHECK(IJ.contains(cI{1}, cJ{2}) && !IJ.contains(cI{1}, cJ{3}));
  // text in a text column's place: a literal, a std::string
  const std::vector<std::string_view> grades{"A", "B"};
  const auto sGRADES = dreamor::index_set::relational::from<cName>(grades).build();
  const std::string grade_b{"B"};
  DREAMOR_CHECK(sGRADES.contains("A") && sGRADES.contains(grade_b) && !sGRADES.contains("C"));

  // membership
  const auto in_ip = sIP.membership();
  DREAMOR_CHECK(in_ip(2, 1) && !in_ip(2, 7) && !in_ip(2, -7) && in_ip(cI{1}, cLater{1}));
  // and its opposite, SQL's NOT IN: a key no set has is not in it
  const auto not_in_ip = !sIP.membership();
  DREAMOR_CHECK(!not_in_ip(2, 1) && not_in_ip(2, 7) && not_in_ip(2, -7) &&
                in_ip(*sIP.begin()) != not_in_ip(*sIP.begin()));
  const auto sITEM_PERIOD =
      dreamor::index_set::relational::from<cI>(items).cross<cPeriod>(std::vector<std::uint32_t>{1, 4}).build();
  DREAMOR_CHECK(sITEM_PERIOD.where(!sIP.membership()).size() == 2);  // (1, 4) and (2, 4)

  // groupBy's slice and contains
  const auto byPeriod = sIP.groupBy<cPeriod>();
  DREAMOR_CHECK(byPeriod.slice(2).size() == 2 && byPeriod.slice(-2).empty() && byPeriod.slice(9).empty());
  DREAMOR_CHECK(byPeriod.contains(3) && !byPeriod.contains(-3) && byPeriod.slice(cLater{1}).size() == 2);
  const auto byItemPeriod = sIP.groupBy<cI, cPeriod>();
  const auto orderedP     = sPERIODS.ordered();
  DREAMOR_CHECK(byItemPeriod.slice(1, 2).size() == 1 && byItemPeriod.slice(cI{1}, *orderedP.next(1)).size() == 1);

  // an Ordered's positions and walks
  const auto* const after_one = orderedP.next(1);
  DREAMOR_CHECK(orderedP.positionOf(2) == std::optional<std::size_t>{1} && !orderedP.positionOf(-2).has_value());
  DREAMOR_CHECK(after_one != nullptr && *after_one == std::tuple{cPeriod{2}} && orderedP.prev(1) == nullptr);
  DREAMOR_CHECK(orderedP.next(1, 2) != nullptr && std::get<cPeriod>(*orderedP.next(1, 2)) == 3);
  const cLater two{2};  // a step count from a column of integers: next(t, k) for a row's lag k
  DREAMOR_CHECK(orderedP.next(1, two) == orderedP.next(1, 2) && orderedP.prevCyclic(1, two) == orderedP.next(1, 1));
  DREAMOR_CHECK(orderedP.next(-1) == nullptr && orderedP.next(7, 1) == nullptr &&
                orderedP.nextCyclic(3) == &orderedP.first());
  DREAMOR_CHECK(orderedP.isFirst(1) && orderedP.isLast(3) && !orderedP.isLast(-3) && orderedP.contains(2));
  DREAMOR_CHECK(!orderedP.contains(-2) && orderedP.nextOf(1) == *after_one &&
                orderedP.prevOf(*after_one, 1) == orderedP.first());
  DREAMOR_CHECK(orderedP.positionOf(orderedP.last()) == std::optional<std::size_t>{2});
  const auto periodsOfItem = sIP.orderedWithin<cI>();
  DREAMOR_CHECK(periodsOfItem.first(2) == std::tuple{cI{2}, cPeriod{1}} &&
                periodsOfItem.last(1) == std::tuple{cI{1}, cPeriod{3}});
  DREAMOR_CHECK(periodsOfItem.next(1, 2) != nullptr && *periodsOfItem.next(1, 2) == std::tuple{cI{1}, cPeriod{3}});
  DREAMOR_CHECK(periodsOfItem.next(1, 2, 1) == periodsOfItem.next(1, 2) && periodsOfItem.prev(2, 1) == nullptr);
  DREAMOR_CHECK(periodsOfItem.positionOf(2, orderedP.last()) == std::optional<std::size_t>{2});

  // set algebra on the same columns in another order: matched by name, in the left's order
  const auto sPI  = sIP.select<cPeriod, cI>();
  const auto late = [](cPeriod t) { return t > 1; };
  static_assert(std::is_same_v<decltype(dreamor::unite(sIP, sPI)), dreamor::IndexSet<cI, cPeriod>>);
  DREAMOR_CHECK(sIP == sPI && sPI == sIP && dreamor::within(sIP, sPI) && dreamor::within(sPI.where(late), sIP));
  DREAMOR_CHECK(!dreamor::within(sIP, sPI.where(late)) && sIP != sPI.where(late));
  DREAMOR_CHECK(std::ranges::equal(dreamor::unite(sIP.where(late), sPI), dreamor::unite(sIP.where(late), sIP)));
  DREAMOR_CHECK(std::ranges::equal(dreamor::intersect(sIP, sPI.where(late)), sIP.where(late)));
  DREAMOR_CHECK(dreamor::difference(sIP, sPI.where(late)).size() == 2 && dreamor::difference(sIP, sPI).empty());

  // a set of one column as the source of another column of its value type
  const auto before = [](cPeriod t, cLater u) { return t < u; };
  const auto sLATER =
      dreamor::index_set::relational::from<cPeriod>(periods).cross<cLater>(sPERIODS).where(before).build();
  DREAMOR_CHECK(std::ranges::equal(
      sLATER, dreamor::index_set::relational::from<cPeriod>(periods).cross<cLater>(periods).where(before).build()));
  DREAMOR_CHECK(dreamor::index_set::relational::from<cLater>(sPERIODS).build().size() == 3);
  const auto later_than = [&sPERIODS](cPeriod t) { return sPERIODS.where([t](cPeriod u) { return u > t; }); };
  DREAMOR_CHECK(std::ranges::equal(
      dreamor::index_set::relational::from<cPeriod>(periods).lateral<cLater>(later_than).build(), sLATER));
  const std::vector<std::tuple<cPeriod>> rows_of_periods{cPeriod{2}, cPeriod{3}};
  const auto stored = [&rows_of_periods](cPeriod /*t*/) -> const std::vector<std::tuple<cPeriod>>& {
    return rows_of_periods;
  };
  DREAMOR_CHECK(dreamor::index_set::relational::from<cPeriod>(periods).lateral<cLater>(stored).build().size() == 6);
  // an entity column from a set of another column over the entity
  dreamor::EntityVector<Node> nodes;
  static_cast<void>(nodes.emplace(Node{1}));
  static_cast<void>(nodes.emplace(Node{2}));
  nodes.freeze();
  const auto sNODES = dreamor::index_set::relational::from<cNode>(nodes).build();
  const auto sARCS  = dreamor::index_set::relational::from<cNode>(nodes).cross<cTo>(sNODES).build();
  DREAMOR_CHECK(sARCS.size() == 4 && sARCS.contains(cNode{nodes.at(1)}, cTo{nodes.at(2)}));
}

// Values written in braces: a temporary container of them, in their order - any integer for a column of integers,
// a literal's text for a text column, a std::string copied by a column that holds its text - for a set built in
// the statement that wrote them; a chain kept in a variable steps on with them too.
void valuesInBraces() {
  const std::vector<std::size_t> values{3, 1};
  DREAMOR_CHECK(std::ranges::equal(dreamor::index_set::relational::from<cI>({3, 1}).build(),
                                   dreamor::index_set::relational::from<cI>(values).build()));
  const auto kept = dreamor::index_set::relational::from<cI>(values);
  DREAMOR_CHECK(std::ranges::equal(
      kept.cross<cJ>({20, 10}).build(),
      dreamor::index_set::relational::from<cI>(values).cross<cJ>(std::vector<std::size_t>{20, 10}).build()));
  const auto sNAMES = dreamor::index_set::relational::from<cName>({"b", "a"}).build();
  DREAMOR_CHECK(sNAMES.size() == 2 && sNAMES.contains("a") && std::get<cName>(*sNAMES.begin()) == "b");
  const auto sTEXTS = dreamor::index_set::relational::from<cText>({std::string{"made"}, std::string{"here"}}).build();
  DREAMOR_CHECK(sTEXTS.size() == 2 && sTEXTS.contains("here"));
}

// The engine builds a chain's rows in another column order than the one its sources brought - the door
// set-builder notation's head goes through - and they are the chain's own rows, in its own row order, with the
// columns put in that order: sources of one column, a set spread first, a set joined on a column a range bound.
void rowsInADeclaredOrder() {
  namespace rel = dreamor::index_set::relational;
  using dreamor::internal::ChainAccess;
  const std::vector<std::size_t> as{1, 2, 3};
  const std::vector<std::size_t> bs{10, 20};
  const auto k_of = [](cI i) { return std::vector<std::size_t>{i.get() * 100, (i.get() * 100) + 1}; };
  const auto odd  = [](cI i, cK k) { return (i.get() + k.get()) % 2 == 1; };
  const auto sIJ  = rel::from<cI>(as).cross<cJ>(bs).build();
  const auto sJL =
      rel::from<cJ>(bs).cross<cL>({7, 8}).where([](cJ j, cL l) { return (j.get() / 10) + l.get() != 9; }).build();

  const auto ranges =
      ChainAccess::build<std::tuple<cK, cI, cJ>>(rel::from<cI>(as).cross<cJ>(bs).lateral<cK>(k_of).where(odd));
  DREAMOR_CHECK(std::ranges::equal(
      ranges, rel::from<cI>(as).cross<cJ>(bs).lateral<cK>(k_of).where(odd).build().select<cK, cI, cJ>()));
  const auto spread = ChainAccess::build<std::tuple<cL, cK, cJ, cI>>(rel::from(sIJ).join(sJL).cross<cK>({5}));
  DREAMOR_CHECK(spread.size() == 6 &&
                std::ranges::equal(spread, rel::from(sIJ).join(sJL).cross<cK>({5}).build().select<cL, cK, cJ, cI>()));
  const auto kept   = rel::from<cJ>(bs).join(sIJ).lateral<cK>(k_of);
  const auto joined = ChainAccess::build<std::tuple<cI, cK, cJ>>(kept);
  DREAMOR_CHECK(std::ranges::equal(joined, kept.build().select<cI, cK, cJ>()));
  static_assert(ChainAccess::visitOrder<std::tuple<cI, cK, cJ>, decltype(kept)>() == decltype(kept)::visitOrder());
}

// A set that is moved from is empty, as a std::vector that is moved from is, and so are a Grouped and an Ordered
// built on a set: every member works on them, and none reads past what it holds.
void movedFrom() {
  namespace rel = dreamor::index_set::relational;
  const std::vector<std::size_t> as{1, 2, 3};
  const std::vector<std::size_t> bs{10, 20};
  auto set   = rel::from<cI>(as).cross<cJ>(bs).build();
  auto moved = std::move(set);
  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move): reading what a move leaves is the test
  DREAMOR_CHECK(moved.size() == 6 && set.empty() && std::ranges::distance(set) == 0 && !set.contains(cI{1}, cJ{10}));
  set = std::move(moved);
  DREAMOR_CHECK(set.size() == 6 && moved.empty());

  auto grouped       = set.groupBy<cI>();
  const auto regroup = std::move(grouped);
  DREAMOR_CHECK(regroup.size() == 3 && grouped.empty() && grouped.slice(cI{1}).empty() && !grouped.contains(cI{1}));

  auto within        = set.orderedWithin<cI>();
  const auto ordered = std::move(within);
  DREAMOR_CHECK(ordered.next(cI{1}, cJ{10}) != nullptr && within.empty() && within.next(cI{1}, cJ{10}) == nullptr);
  DREAMOR_CHECK(!within.positionOf(cI{1}, cJ{10}).has_value() && !within.contains(cI{1}, cJ{10}));
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

// A set, a Grouped and an Ordered constructed with no arguments are empty, as moved-from ones: every member works
// and finds nothing; one assigned a set, or the grouping or order of a set, is whole. A set so made is none().
// Apart from noRows: GCC 16 at -O1 -g warned -Wuninitialized on an SR.* temporary of noRows's Membership copy when
// these lines were there (a false positive: not at -g0 or -g1; PR 123651's family).
void constructedEmpty() {
  namespace rel = dreamor::index_set::relational;
  const std::vector<std::size_t> as{1, 2, 3};
  const auto sIJ = rel::from<cI>(as).cross<cJ>(as).build();
  using Set      = dreamor::IndexSet<cI, cJ>;
  static_assert(std::is_nothrow_default_constructible_v<Set>);
  Set sLATER;
  DREAMOR_CHECK(sLATER.empty() && sLATER == Set::none() && !sLATER.contains(cI{1}, cJ{1}));
  sLATER = sIJ;
  DREAMOR_CHECK(sLATER == sIJ && sLATER.contains(cI{1}, cJ{1}));
  dreamor::Grouped<Set, cI> byI;
  dreamor::Ordered<Set> whole;
  dreamor::Ordered<Set, cI> withinI;
  DREAMOR_CHECK(byI.empty() && byI.begin() == byI.end() && byI.slice(cI{1}).empty());
  DREAMOR_CHECK(!byI.contains(cI{1}) && whole.empty() && !whole.contains(cI{1}, cJ{1}));
  DREAMOR_CHECK(!whole.positionOf(cI{1}, cJ{1}).has_value() && whole.next(cI{1}, cJ{1}) == nullptr);
  DREAMOR_CHECK(withinI.empty() && withinI.next(cI{1}, cJ{1}) == nullptr && !withinI.isFirst(cI{1}, cJ{1}));
  byI     = sIJ.groupBy<cI>();
  whole   = sIJ.ordered();
  withinI = sIJ.orderedWithin<cI>();
  DREAMOR_CHECK(byI.size() == 3 && byI.slice(cI{2}).size() == 3);
  DREAMOR_CHECK(whole.size() == 9 && whole.positionOf(cI{2}, cJ{1}) == std::optional<std::size_t>{3});
  DREAMOR_CHECK(withinI.positionOf(cI{2}, cJ{3}) == std::optional<std::size_t>{2} && withinI.isFirst(cI{3}, cJ{1}));
}

// none(): a set of the type's columns with no rows, allocating nothing. Every
// derivation and operation takes it, and it equals an empty set built otherwise; the empty storage it shares, index
// included, serves every none() alike.
void noRows() {
  namespace rel = dreamor::index_set::relational;
  const std::vector<std::size_t> as{1, 2, 3};
  const auto sIJ     = rel::from<cI>(as).cross<cJ>(as).build();
  const auto sNONE   = dreamor::IndexSet<cI, cJ>::none();
  const auto any_row = [](cI /*i*/) { return true; };
  static_assert(noexcept(dreamor::IndexSet<cI, cJ>::none()));
  static_assert(std::is_same_v<decltype(sNONE), const dreamor::IndexSet<cI, cJ>>);
  DREAMOR_CHECK(sNONE.empty() && sNONE.begin() == sNONE.end() && !sNONE.contains(cI{1}, cJ{1}));
  DREAMOR_CHECK(sNONE == rel::from<cI>(std::vector<std::size_t>{}).cross<cJ>(as).build());
  DREAMOR_CHECK(sNONE.where(any_row).empty() && sNONE.select<cJ>().empty() && sNONE.orderBy(any_row).empty());
  DREAMOR_CHECK(sNONE.groupBy<cI>().empty() && sNONE.ordered().empty() && sNONE.orderedWithin<cI>().empty());
  DREAMOR_CHECK(dreamor::unite(sIJ, sNONE) == sIJ && dreamor::unite(sNONE, sIJ) == sIJ);
  DREAMOR_CHECK(dreamor::intersect(sIJ, sNONE).empty() && dreamor::difference(sIJ, sNONE) == sIJ);
  DREAMOR_CHECK(dreamor::difference(sNONE, sIJ).empty() && dreamor::within(sNONE, sIJ) && !dreamor::within(sIJ, sNONE));
  // The opposite condition is called, not passed to where: GCC 16 with -g warns -Wmaybe-uninitialized on the
  // Membership<..., false> that where copies here, and not without -g (a false positive; difference covers the set).
  const auto not_in_none = !sNONE.membership();
  DREAMOR_CHECK(sIJ.where(sNONE.membership()).empty() && not_in_none(cI{1}, cJ{1}));
  DREAMOR_CHECK(rel::from(sIJ).join(sNONE).build().empty() && rel::from(sNONE).cross<cK>(as).build().empty());
  // A second none() shares the storage, and the index the first built: it still finds nothing.
  const auto sALSO_NONE = dreamor::IndexSet<cI, cJ>::none();
  DREAMOR_CHECK(sALSO_NONE.empty() && !sALSO_NONE.contains(cI{2}, cJ{3}) && sALSO_NONE == sNONE);
}

// Each row with the first and last rows of its group, read through a temporary slice's front() and back() inside a
// callable: the shape in which GCC warned -Wnull-dereference before they reached unreachable() on an empty slice.
void frontOfSlices() {
  namespace rel = dreamor::index_set::relational;
  const std::vector<std::size_t> as{3, 1, 2};
  const auto sIJ       = rel::from<cI>(as).cross<cJ>(as).build();
  const auto byI       = sIJ.groupBy<cI>();
  const auto with_head = [&byI](cI i, cJ j) {
    // The slice a temporary, the rows copied: GCC 13 takes a reference from a temporary's front() as dangling.
    const auto [gi, firstJ] = byI.slice(i).front();
    const auto [li, lastJ]  = byI.slice(i).back();
    return std::tuple<cI, cJ, cK, cL>{i, j, cK{firstJ.get()}, cL{lastJ.get()}};
  };
  const auto sHEADS = sIJ.select(with_head);
  DREAMOR_CHECK(sHEADS.size() == 9);
  DREAMOR_CHECK(std::ranges::all_of(sHEADS, [](const auto& row) {
    return std::get<cK>(row) == 3 && std::get<cL>(row) == 2;  // each i's j come 3, 1, 2
  }));
}

// A step count anywhere in its type's range: a walk past an end of the set gives no row, a cyclic walk wraps by the
// count modulo the number of rows, and an unsigned count above PTRDIFF_MAX walks forward.
void extremeSteps() {
  // Positions 0 to 3: 2^63 is 0 modulo 4, and 2^63 - 1 and 2^64 - 1 are 3.
  const std::vector<std::size_t> I{3, 1, 2, 4};
  const auto II         = dreamor::index_set::relational::from<cI>(I).build();
  const auto order      = II.ordered();
  constexpr auto kMost  = std::numeric_limits<std::ptrdiff_t>::max();
  constexpr auto kLeast = std::numeric_limits<std::ptrdiff_t>::min();
  constexpr auto kAll   = std::numeric_limits<std::uint64_t>::max();
  const auto value      = [](const std::tuple<cI>* row) { return row == nullptr ? 0 : std::get<cI>(*row).get(); };

  DREAMOR_CHECK(order.next(cI{1}, kMost) == nullptr && order.prev(cI{1}, kMost) == nullptr);
  DREAMOR_CHECK(order.next(cI{1}, kLeast) == nullptr && order.prev(cI{1}, kLeast) == nullptr);
  DREAMOR_CHECK(order.next(cI{2}, kAll) == nullptr && order.prev(cI{2}, kAll) == nullptr);
  DREAMOR_CHECK(value(order.nextCyclic(cI{1}, kMost)) == 3 && value(order.prevCyclic(cI{1}, kMost)) == 2);
  DREAMOR_CHECK(value(order.nextCyclic(cI{1}, kLeast)) == 1 && value(order.prevCyclic(cI{1}, kLeast)) == 1);
  DREAMOR_CHECK(value(order.nextCyclic(cI{2}, kAll)) == 1 && value(order.prevCyclic(cI{2}, kAll)) == 4);
  DREAMOR_CHECK(value(order.next(cI{3}, std::uint64_t{3})) == 4 && value(order.next(cI{4}, -3)) == 3);
}
}  // namespace

template <typename S>
constexpr bool canPushBack = requires(S& s) { s.push_back(std::declval<typename S::value_type>()); };

namespace {

// Every form of a step gives the same rows: on a chain kept in a variable (const&) as on one being written (&&), with
// a container or a set passed as a variable or as a temporary.
void stepForms() {
  namespace rel = dreamor::index_set::relational;
  const std::vector<std::size_t> as{1, 2};
  const std::vector<std::size_t> bs{3, 4};
  const auto sJ      = rel::from<cJ>(bs).build();
  const auto sJK     = rel::from<cJ>(bs).cross<cK>(bs).build();
  const auto crossed = rel::from<cI>(as).cross<cJ>(bs).build();
  const auto joined  = rel::from<cI>(as).cross<cJ>(bs).join(sJK).build();
  const auto kept    = rel::from<cI>(as);
  const auto keptIJ  = rel::from<cI>(as).cross<cJ>(bs);
  const auto js      = [&bs](cI /*i*/) -> const std::vector<std::size_t>& { return bs; };

  // a column's values, a temporary container, on a kept chain
  DREAMOR_CHECK(std::ranges::equal(kept.cross<cJ>(std::vector<std::size_t>{3, 4}).build(), crossed));
  // a set's rows, a variable or a temporary, on a kept chain and on one being written
  DREAMOR_CHECK(std::ranges::equal(kept.cross(sJ).build(), crossed));
  DREAMOR_CHECK(std::ranges::equal(kept.cross(rel::from<cJ>(bs).build()).build(), crossed));
  DREAMOR_CHECK(std::ranges::equal(rel::from<cI>(as).cross(rel::from<cJ>(bs).build()).build(), crossed));
  // a join with a temporary set, on a chain being written and on a kept one
  DREAMOR_CHECK(std::ranges::equal(
      rel::from<cI>(as).cross<cJ>(bs).join(rel::from<cJ>(bs).cross<cK>(bs).build()).build(), joined));
  DREAMOR_CHECK(std::ranges::equal(keptIJ.join(rel::from<cJ>(bs).cross<cK>(bs).build()).build(), joined));
  // rows in a temporary container, which the chain holds by reference until build(): on a chain being written and on
  // a kept one, to cross, to join and to start from
  using RowsJ  = std::vector<std::tuple<cJ>>;
  using RowsJK = std::vector<std::tuple<cJ, cK>>;
  using RowsIJ = std::vector<std::tuple<cI, cJ>>;
  DREAMOR_CHECK(std::ranges::equal(rel::from<cI>(as).cross(RowsJ{{cJ{3}}, {cJ{4}}}).build(), crossed));
  DREAMOR_CHECK(std::ranges::equal(kept.cross(RowsJ{{cJ{3}}, {cJ{4}}}).build(), crossed));
  const RowsJK jk{{cJ{3}, cK{3}}, {cJ{3}, cK{4}}, {cJ{4}, cK{3}}, {cJ{4}, cK{4}}};
  DREAMOR_CHECK(std::ranges::equal(rel::from<cI>(as).cross<cJ>(bs).join(RowsJK{jk}).build(), joined));
  DREAMOR_CHECK(std::ranges::equal(keptIJ.join(RowsJK{jk}).build(), joined));
  DREAMOR_CHECK(std::ranges::equal(
      rel::from(RowsIJ{{cI{1}, cJ{3}}, {cI{1}, cJ{4}}, {cI{2}, cJ{3}}, {cI{2}, cJ{4}}}).build(), crossed));
  // a lateral step on a kept chain, and a chain from a temporary set
  DREAMOR_CHECK(std::ranges::equal(kept.lateral<cJ>(js).build(), crossed));
  DREAMOR_CHECK(std::ranges::equal(rel::from(rel::from<cI>(as).cross<cJ>(bs).build()).build(), crossed));
}

// A group's rows are a random-access range, and every operation of its iterator reaches the same rows; the groups
// themselves are a forward range.
void groupIterators() {
  namespace rel = dreamor::index_set::relational;
  const std::vector<std::size_t> as{1, 2};
  const std::vector<std::size_t> bs{3, 4, 5, 6};
  const auto byI  = rel::from<cI>(as).cross<cJ>(bs).build().groupBy<cI>();
  const auto rows = byI.slice(cI{1});
  auto it         = rows.begin();
  DREAMOR_CHECK(it.operator->() == &*it && &it[2] == &*(it + 2) && &*(2 + it) == &it[2]);
  const auto first = it++;
  DREAMOR_CHECK(first == rows.begin() && it - first == 1);
  const auto second = it--;
  DREAMOR_CHECK(it == first && second - first == 1 && --(it + 2) == second);
  it += 3;
  it -= 2;
  DREAMOR_CHECK(it == second && it - 1 == first && (first <=> second) == std::strong_ordering::less);
  auto group         = byI.begin();
  const auto initial = group++;
  DREAMOR_CHECK(initial == byI.begin() && group != initial);
}

}  // namespace

int main() try {
  const std::vector<std::size_t> I{3, 1, 2, 4};
  const std::vector<std::size_t> J{10, 20, 30};
  const auto IJ = dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).build();
  const std::vector<std::size_t> nothing;
  const auto none = dreamor::index_set::relational::from<cI>(nothing)
                        .cross<cJ>(nothing)
                        .build();  // the empty set: a chain over nothing
  const auto II   = dreamor::index_set::relational::from<cI>(I).build();
  const auto even = [](cI i) { return i.get() % 2 == 0; };
  const auto big  = [](cJ j) { return j.get() >= 20; };

  // A walk over groups gives each group by value: a C++20 forward iterator, and a C++17 input iterator, as
  // std::views::transform's is.
  using GroupIterator = std::ranges::iterator_t<const decltype(IJ.groupBy<cI>())>;
  static_assert(std::forward_iterator<GroupIterator>);
  static_assert(std::is_same_v<std::iterator_traits<GroupIterator>::iterator_category, std::input_iterator_tag>);

  // a column compares with a value as its value does, either side, without ambiguity; integers by their values
  {
    const std::size_t two = 2;
    const cI i{2};
    DREAMOR_CHECK(i == 2 && i == two && 2 == i && i != 3 && !(i == -1) && i == cI{2});
    dreamor::EntityVector<Node> nodes;
    static_cast<void>(nodes.emplace(Node{1}));
    static_cast<void>(nodes.emplace(Node{2}));
    nodes.freeze();
    const dreamor::Ref<Node> first = nodes.at(1);
    const cNode n{first};
    DREAMOR_CHECK(n == first && first == n && n != nodes.at(2) && n != dreamor::Ref<Node>{});
    const cName name{"plate"};
    DREAMOR_CHECK(name == "plate" && name != std::string("coils"));
    const auto is_three = [](cI k) { return k == 3; };
    DREAMOR_CHECK(IJ.where(is_three).size() == 3);
    static_assert(!canCompare<cNode, std::size_t> && !canCompare<cI, dreamor::Ref<Node>>);
  }

  // read-only, copy shares rows
  static_assert(!canPushBack<dreamor::IndexSet<cI>>);
  static_assert(std::ranges::random_access_range<dreamor::IndexSet<cI, cJ>> &&
                std::ranges::sized_range<dreamor::IndexSet<cI>>);
  const auto copy = IJ;  // NOLINT(performance-unnecessary-copy-initialization): the copy is what is checked
  DREAMOR_CHECK(copy.begin() == IJ.begin() && IJ.size() == 12);

  // where: AND of predicates, source untouched, order preserved
  DREAMOR_CHECK(IJ.where(even).size() == 6);
  DREAMOR_CHECK(IJ.where(even, big).size() == 4);
  DREAMOR_CHECK(std::ranges::equal(
      IJ.where(even, big), dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).where(even).where(big).build()));
  // The builder's where() takes several predicates too, the same AND as chaining them.
  DREAMOR_CHECK(
      std::ranges::equal(dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).where(even, big).build(),
                         dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).where(even).where(big).build()));
  DREAMOR_CHECK(!IJ.empty() && none.empty() && IJ.where(even, big).size() == 4);
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
  DREAMOR_CHECK(byI.size() == 4 && std::get<cI>(byI.at(0).key).get() == 3);
  const auto s1 = byI.slice(cI{1});
  static_assert(std::ranges::random_access_range<decltype(s1)> && std::ranges::borrowed_range<decltype(s1)>);
  DREAMOR_CHECK(s1.size() == 3 && !s1.empty());
  DREAMOR_CHECK(std::get<cJ>(s1[0]).get() == 10 && std::get<cJ>(s1.back()).get() == 30 &&
                std::get<cJ>(s1.front()).get() == 10);
  DREAMOR_CHECK((s1.end() - s1.begin()) == 3 && std::get<cJ>(*(s1.begin() + 2)).get() == 30);
  DREAMOR_CHECK(byI.slice(cI{99}).empty() && !byI.contains(cI{99}) && byI.slice(cI{2}).size() == 3);
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
    DREAMOR_CHECK(std::get<cI>(group.key) == std::get<cI>(group.rows.front()));
    ++groups;
  }
  DREAMOR_CHECK(pairs == 12 && groups == 4);
  DREAMOR_CHECK(std::ranges::equal(byI.regrouped().select<cI>(), II));
  DREAMOR_CHECK_THROWS(std::out_of_range, byI.at(4));

  // membership: as a predicate in a builder where
  const auto inEven = IJ.where(even).membership();
  DREAMOR_CHECK(dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).where(inEven).build().size() == 6);

  // ordered
  const auto T          = II.ordered();
  const auto* beforeOne = T.prev(cI{1});
  DREAMOR_CHECK(beforeOne != nullptr && std::get<cI>(*beforeOne).get() == 3 && T.next(cI{4}) == nullptr);
  DREAMOR_CHECK(&T.prevOf(cI{1}) == beforeOne && &T.nextOf(cI{3}) == T.next(cI{3}));  // the row itself, where it is

  // ordered within groups - next(j, S[i]): each i's rows here are interleaved with the other's
  {
    const std::vector<std::size_t> Js{10, 20, 30};
    const std::vector<std::size_t> Is{1, 2};
    const auto rowsJI =
        dreamor::index_set::relational::from<cJ>(Js).cross<cI>(Is).build();  // (10,1) (10,2) (20,1) (20,2) ...
    const auto withinI = rowsJI.orderedWithin<cI>();
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
    DREAMOR_CHECK(withinI.positionOf(cJ{20}, cI{2}) == std::optional<std::size_t>{1} && withinI.isFirst(cJ{10}, cI{2}));
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
    const auto alone = rowsJI.orderedWithin<cJ, cI>();  // every row its own group
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
  // NOLINTNEXTLINE(readability-container-size-empty): this tests operator== against the empty set
  DREAMOR_CHECK(IJ.where([](cI) { return false; }) == none);

  // within: a subset test, read as written - every row of the first in the second, in any order
  const auto evens = IJ.where(even);
  // a noexcept predicate or lateral source reads as any other: noexcept is part of operator()'s type
  const auto even_noexcept = [](cI i) noexcept { return i.get() % 2 == 0; };
  const auto js_noexcept   = [&J](cI) noexcept -> const std::vector<std::size_t>& { return J; };
  DREAMOR_CHECK(std::ranges::equal(IJ.where(even_noexcept), evens));
  DREAMOR_CHECK(
      std::ranges::is_permutation(dreamor::index_set::relational::from<cI>(I).lateral<cJ>(js_noexcept).build(), IJ));
  DREAMOR_CHECK(dreamor::within(evens, IJ) && !dreamor::within(IJ, evens));
  DREAMOR_CHECK(dreamor::within(IJ, IJ) && dreamor::within(IJ, reversed) && dreamor::within(evens, evens.where(even)));
  DREAMOR_CHECK(dreamor::within(none, IJ) && !dreamor::within(IJ, none));
  DREAMOR_CHECK(!dreamor::within(evens, dreamor::difference(IJ, evens)));  // same size, other rows
  DREAMOR_CHECK(
      !dreamor::within(dreamor::unite(evens, II.select([](cI i) { return std::tuple<cI, cJ>{i, cJ{99}}; })), IJ));

  // the visit order: with no predicate to tell them apart, a lateral source goes before a
  // container - its list per binding is usually the shorter - and containers keep declaration order
  {
    const std::vector<std::size_t> As{1, 2};
    const std::vector<std::size_t> Bs{3, 4};
    const std::vector<std::size_t> Cs{5, 6};
    const auto c_of = [&Cs](cI) -> const std::vector<std::size_t>& { return Cs; };
    using Planned   = decltype(dreamor::index_set::relational::from<cI>(As).cross<cJ>(Bs).lateral<cK>(c_of));
    static_assert(Planned::visitOrder() == "cI, cK, cJ");
    DREAMOR_CHECK(dreamor::index_set::relational::from<cI>(As).cross<cJ>(Bs).lateral<cK>(c_of).build().size() == 8);
    // read off a chain kept in a variable, as a model logs it; a set source's columns in braces
    const auto chain = dreamor::index_set::relational::from<cI>(As).cross<cJ>(Bs).lateral<cK>(c_of);
    DREAMOR_CHECK(chain.visitOrder() == "cI, cK, cJ");
    const auto sIJ = dreamor::index_set::relational::from<cI>(As).cross<cJ>(Bs).build();
    static_assert(decltype(dreamor::index_set::relational::from(sIJ).cross<cK>(Cs))::visitOrder() == "{cI, cJ}, cK");
  }

  // a chain kept in a variable steps on by copy, over named sources; one over a temporary container is built
  // in the statement that made it; an interval is a view, held by value, so a chain over one may be kept
  const auto kept = dreamor::index_set::relational::from<cI>(I);
  DREAMOR_CHECK(std::ranges::is_permutation(kept.cross<cJ>(J).build(), IJ) && kept.build().size() == I.size());
  DREAMOR_CHECK(dreamor::index_set::relational::from<cI>(std::vector<std::size_t>{3, 1}).cross<cJ>(J).build().size() ==
                2 * J.size());
  const auto upto = dreamor::index_set::relational::from<cI>(dreamor::interval::closed(std::size_t{1}, std::size_t{3}));
  DREAMOR_CHECK(upto.build().size() == 3);

  // a set spread into a new one
  const auto IJK = dreamor::index_set::relational::from(IJ).build();
  DREAMOR_CHECK(std::ranges::equal(IJK, IJ));

  // two sets sharing a column: the natural join on it - the rows of the product that agree
  const std::vector<std::size_t> K{5, 6};
  const auto JK     = dreamor::index_set::relational::from<cJ>(J)
                          .cross<cK>(K)
                          .where([](cJ j, cK k) { return (j.get() / 10) + k.get() != 8; })
                          .build();
  const auto joined = dreamor::index_set::relational::from(IJ).join(JK).build();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(joined)>, std::tuple<cI, cJ, cK>>);
  DREAMOR_CHECK(JK.size() == 4 && joined.size() == 16);
  DREAMOR_CHECK(std::ranges::is_permutation(
      joined, dreamor::index_set::relational::from(IJ).cross<cK>(K).where(JK.membership()).build()));
  DREAMOR_CHECK(std::ranges::is_permutation(
      joined, dreamor::index_set::relational::from(JK).join(IJ).build().select<cI, cJ, cK>()));
  DREAMOR_CHECK(dreamor::index_set::relational::from(IJ).join(JK).where(even).build().size() == 8);
  // a chain of three, each joined on what the one before it bound
  const std::vector<std::size_t> L{7, 8};
  const auto KL    = dreamor::index_set::relational::from<cK>(K)
                         .cross<cL>(L)
                         .where([](cK k, cL l) { return k.get() + 2 == l.get(); })
                         .build();
  const auto chain = dreamor::index_set::relational::from(IJ).join(JK).join(KL).build();
  DREAMOR_CHECK(chain.size() == 16 && std::ranges::all_of(chain, [](const auto& row) {
                  return std::get<cK>(row).get() + 2 == std::get<cL>(row).get();
                }));
  // a set inside another one's columns filters it, as where(membership()) does
  const auto II_even = II.where(even);
  DREAMOR_CHECK(std::ranges::equal(dreamor::index_set::relational::from(IJ).join(II_even).build(),
                                   IJ.where(II_even.membership())));
  DREAMOR_CHECK(
      std::ranges::is_permutation(dreamor::index_set::relational::from(II_even).join(IJ).build(), IJ.where(even)));
  // no match, no row
  const auto IJ_big = IJ.where(big);
  const auto JK_ten = JK.where([](cJ j) { return j.get() == 10; });
  DREAMOR_CHECK(dreamor::index_set::relational::from(IJ_big).join(JK_ten).build().empty());
  // joined on a column a range or a lateral source brought, not a set
  const std::vector<std::size_t> evens_of_I{2, 4};
  DREAMOR_CHECK(std::ranges::is_permutation(dreamor::index_set::relational::from<cI>(evens_of_I).join(IJ).build(),
                                            dreamor::index_set::relational::from<cI>(evens_of_I).cross<cJ>(J).build()));
  const auto twenty = [](cI) { return std::vector<std::size_t>{20}; };
  DREAMOR_CHECK(
      std::ranges::is_permutation(dreamor::index_set::relational::from<cI>(I).lateral<cJ>(twenty).join(IJ_big).build(),
                                  IJ.where([](cJ j) { return j.get() == 20; })));
  const auto shared_then_new = dreamor::index_set::relational::from<cJ>(J).join(JK_ten).cross<cL>(L).build();
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(shared_then_new)>, std::tuple<cJ, cK, cL>>);
  DREAMOR_CHECK(shared_then_new.size() == JK_ten.size() * L.size());

  // IndexSet{records, row_of}: a row for each record, in the records' order
  {
    const std::vector<Link> links{{3, 10}, {1, 20}, {3, 30}};
    // the set's columns, found by their types in any order
    const auto named = [](const Link& link) { return std::tuple<cJ, cI>{link.to, link.from}; };
    const dreamor::IndexSet<cI, cJ> sLINKS{links, named};
    DREAMOR_CHECK(sLINKS.size() == 3 && std::get<cI>(*sLINKS.begin()) == 3 && std::get<cJ>(*sLINKS.begin()) == 10);
    DREAMOR_CHECK(std::ranges::is_permutation(sLINKS.where(big), IJ.where(big).where(sLINKS.membership())));
    // a lone column, and a temporary range: the rows are copied
    const auto from_of = [](std::size_t i) { return cI{i}; };
    const dreamor::IndexSet<cI> sFROM{std::vector<std::size_t>{4, 2}, from_of};
    DREAMOR_CHECK(
        std::ranges::equal(sFROM, dreamor::index_set::relational::from<cI>(std::vector<std::size_t>{4, 2}).build()));
    // a view that makes its records
    const dreamor::IndexSet<cI> sIOTA{std::views::iota(std::size_t{0}, std::size_t{3}), from_of};
    DREAMOR_CHECK(sIOTA.size() == 3 && std::get<cI>(sIOTA.begin()[2]) == 2);
    // plain values, each filling the column of its value type: a Ref, a number
    dreamor::EntityVector<Node> nodes;
    static_cast<void>(nodes.emplace(Node{1}));
    static_cast<void>(nodes.emplace(Node{2}));
    nodes.freeze();
    const auto valued = [&nodes](const Link& link) { return std::tuple{link.to, nodes.at(link.from == 3 ? 1 : 2)}; };
    const dreamor::IndexSet<cNode, cJ> sVALUED{links, valued};
    DREAMOR_CHECK(sVALUED.size() == 3 && std::get<cNode>(sVALUED.begin()[1]) == nodes.at(2) &&
                  std::get<cJ>(sVALUED.begin()[2]) == 30);
    // a text column from named records: its rows show the records' own strings
    const std::vector<std::string> labels{"press-line-number-one", "press-line-number-two"};
    const auto label_of = [](const std::string& label) { return cName{label}; };
    const dreamor::IndexSet<cName> sLABELS{labels, label_of};
    DREAMOR_CHECK(sLABELS.size() == 2 &&
                  std::string_view{std::get<cName>(sLABELS.begin()[1]).get()}.data() == labels[1].data());
    // an empty range, an empty set
    const dreamor::IndexSet<cI> sNONE{std::vector<std::size_t>{}, from_of};
    DREAMOR_CHECK(sNONE.empty());
    // a member, built in its class's initializer list from the members before it
    const Network network{links};
    DREAMOR_CHECK(std::ranges::equal(network.links(), sLINKS));
  }
  valuesInKeyPlaces();
  rowOfValuesByType();
  valuesInBraces();
  rowsInADeclaredOrder();
  movedFrom();
  constructedEmpty();
  noRows();
  frontOfSlices();
  extremeSteps();
  stepForms();
  groupIterators();
  return report("index_set");
} catch (...) { return ::uncaught(); }
