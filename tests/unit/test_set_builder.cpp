// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// index_set::set_builder: sets written as set-builder notation reads them, each against the same set written as a
// chain - a product with a list per row and a condition, a set's rows with a range, a join on a column two sets
// share, a column bound already among values and among a list per row, one set as two columns, values in braces,
// an interval per row; a column bound already among an interval, and among an interval or a set per row; the head's
// order whatever the generators' order; a temporary container, and a named of().
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Item {
  std::string name;
  double time = 0;

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Machine {
  std::string name;
  double capacity = 0;
  std::vector<dreamor::Ref<Item>> items;  // ITEMS[m]: what m can make

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};
enum class Shift : std::uint8_t { kDay, kNight };

struct DemandRecord {
  std::string item;
  int period = 0;
};

DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_VALUE_COLUMN(cPeriod, int);
DREAMOR_VALUE_COLUMN(cLater, int);
DREAMOR_VALUE_COLUMN(cShift, Shift);
DREAMOR_VALUE_COLUMN(cBatch, std::size_t);

constexpr int kLast = 3;  // the last period

template <typename A, typename B>
bool sameRows(const A& a, const B& b) {
  return a.size() == b.size() && std::ranges::is_permutation(a, b);
}

std::vector<int> periodsMade() {
  return {1, 2, 3};
}

// The items a machine makes, as a type of its own, so that a test can pass the callable as an rvalue.
struct ItemsOf {
  const std::vector<dreamor::Ref<Item>>& operator()(cMachine m) const { return m->items; }
};

}  // namespace

int main() try {
  using namespace dreamor::index_set::set_builder;
  namespace rel = dreamor::index_set::relational;

  dreamor::EntityVector<Item> items;
  static_cast<void>(items.emplace(Item{"A", 1.0}));
  static_cast<void>(items.emplace(Item{"B", 2.0}));
  static_cast<void>(items.emplace(Item{"C", 4.0}));
  items.freeze();
  dreamor::EntityVector<Machine> machines;
  static_cast<void>(machines.emplace(Machine{"M1", 100.0, {items.at("A"), items.at("B")}}));
  static_cast<void>(machines.emplace(Machine{"M2", 60.0, {items.at("B"), items.at("C")}}));
  machines.freeze();
  const std::vector<int> periods{1, 2, 3};
  const std::vector<dreamor::Ref<Item>> urgent{items.at("B")};
  const std::vector<DemandRecord> demand{{"A", 1}, {"B", 2}, {"B", 3}, {"C", 3}};

  const auto items_of   = [](cMachine m) -> const std::vector<dreamor::Ref<Item>>& { return m->items; };
  const auto fits_batch = [](cMachine m, cItem i) { return 30 * i->time <= m->capacity; };
  const auto demand_row = [&items](const DemandRecord& d) {
    return std::tuple<cItem, cPeriod>{items.at(d.item), d.period};
  };
  const auto earlier   = [](cPeriod t, cLater u) { return t < u; };
  const auto from_t_on = [](cPeriod t) { return dreamor::interval::closed(t.get(), kLast); };

  const auto sPERIODS = rel::from<cPeriod>(periods).build();
  const dreamor::IndexSet<cItem, cPeriod> sDEMAND{demand, demand_row};

  // each machine, each item it makes, each period, where a batch of 30 fits: the chain below's rows
  const auto sMAKE = of<cMachine, cItem, cPeriod>(
                         in<cMachine>(machines), in<cItem>(items_of), in<cPeriod>(dreamor::interval::closed(1, kLast)))
                         .where(fits_batch)
                         .build();
  static_assert(std::is_same_v<decltype(sMAKE), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  DREAMOR_CHECK(sMAKE.size() == 9 && std::ranges::equal(sMAKE,
                                                        rel::from<cMachine>(machines)
                                                            .lateral<cItem>(items_of)
                                                            .cross<cPeriod>(dreamor::interval::closed(1, kLast))
                                                            .where(fits_batch)
                                                            .build()));

  // the routes, then every route in every period: a set's two columns bound by one generator
  const auto sROUTES  = of<cMachine, cItem>(in<cMachine>(machines), in<cItem>(items_of)).build();
  const auto sPRODUCE = of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(sROUTES), in<cPeriod>(sPERIODS)).build();
  DREAMOR_CHECK(sPRODUCE.size() == 12 && sameRows(sPRODUCE, rel::from(sROUTES).cross<cPeriod>(sPERIODS).build()));
  // a set's columns named in any order
  DREAMOR_CHECK(sameRows(of<cMachine, cItem>(in<cItem, cMachine>(sROUTES)).build(), sROUTES));

  // the routes matched with the demand on the item, the column the two share
  const auto sSUPPLY = of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(sROUTES), in<cItem, cPeriod>(sDEMAND)).build();
  DREAMOR_CHECK(sSUPPLY.size() == 6 && sameRows(sSUPPLY, rel::from(sROUTES).join(sDEMAND).build()));

  // the routes of an urgent item: the item bound already, so the generator is a condition, looked up
  const auto sURGENT = rel::from<cItem>(urgent).build();
  const auto sRUSH   = of<cMachine, cItem>(in<cMachine, cItem>(sROUTES), in<cItem>(urgent)).build();
  DREAMOR_CHECK(sRUSH.size() == 2 && sameRows(sRUSH, sROUTES.where(sURGENT.membership())));
  // the same over values in braces, and over a temporary container, built in the statement
  DREAMOR_CHECK(sameRows(of<cMachine, cItem>(in<cMachine, cItem>(sROUTES), in<cItem>({items.at("B")})).build(), sRUSH));
  DREAMOR_CHECK(of<cPeriod>(in<cPeriod>(sPERIODS), in<cPeriod>(periodsMade())).build().size() == 3);

  // every machine with every item, kept where the item is in the machine's list: a list per row as a condition
  DREAMOR_CHECK(
      sameRows(of<cMachine, cItem>(in<cMachine>(machines), in<cItem>(items), in<cItem>(items_of)).build(), sROUTES));

  // every pair of periods t < u: one set, two columns
  const auto sPAIRS = of<cPeriod, cLater>(in<cPeriod>(sPERIODS), in<cLater>(sPERIODS)).where(earlier).build();
  DREAMOR_CHECK(sPAIRS.size() == 3 &&
                sameRows(sPAIRS, rel::from(sPERIODS).cross<cLater>(sPERIODS).where(earlier).build()));

  // every machine in each shift, the shifts written in braces
  const auto sCREW = of<cMachine, cShift>(in<cMachine>(machines), in<cShift>({Shift::kDay, Shift::kNight})).build();
  DREAMOR_CHECK(sCREW.size() == 4);

  // every period t, with each period u from t to the last
  const auto sSPAN =
      of<cPeriod, cLater>(in<cPeriod>(dreamor::interval::closed(1, kLast)), in<cLater>(from_t_on)).build();
  DREAMOR_CHECK(sSPAN.size() == 6);

  // The same pairs t <= u with u bound already: the interval per row is a condition. So is a set of one column per
  // row, of the generator's column or of another of the same value type. Each failed to compile: the condition
  // compared the column with the interval's values and the set's rows by ==.
  const auto sLATER     = rel::from<cLater>(periods).build();
  const auto any_period = [&sPERIODS](cPeriod /*t*/) -> const dreamor::IndexSet<cPeriod>& { return sPERIODS; };
  const auto any_later  = [&sLATER](cPeriod /*t*/) -> const dreamor::IndexSet<cLater>& { return sLATER; };
  DREAMOR_CHECK(
      sameRows(of<cPeriod, cLater>(in<cPeriod>(sPERIODS), in<cLater>(sPERIODS), in<cLater>(from_t_on)).build(), sSPAN));
  DREAMOR_CHECK(
      of<cPeriod, cLater>(in<cPeriod>(sPERIODS), in<cLater>(sPERIODS), in<cLater>(any_period)).build().size() == 9);
  DREAMOR_CHECK(of<cPeriod, cLater>(in<cPeriod>(sPERIODS), in<cLater>(sLATER), in<cLater>(any_later)).build().size() ==
                9);

  // An interval as a condition on a column of std::size_t, once and per row. The first failed to compile in a debug
  // build: the check of a source's rows copied them with std::vector's constructor from two iterators, which an
  // interval of std::size_t's iterator is not under -std=c++20. A value out of the column's range, -1 here, is among
  // no row's values.
  const std::vector<std::size_t> batches{0, 1, 2, 3};
  const auto up_to_period = [](cPeriod t) { return dreamor::interval::closed(-1, t.get() - 2); };
  DREAMOR_CHECK(of<cBatch>(in<cBatch>(batches), in<cBatch>(dreamor::interval::closed(std::size_t{1}, std::size_t{2})))
                    .build()
                    .size() == 2);
  DREAMOR_CHECK(
      of<cPeriod, cBatch>(in<cPeriod>(sPERIODS), in<cBatch>(batches), in<cBatch>(up_to_period)).build().size() ==
      3);  // t = 2 keeps batch 0, t = 3 batches 0 and 1

  // the head's order, whatever order the generators are written in: the engine plans the visit
  const auto sWORST = of<cMachine, cItem, cPeriod>(in<cPeriod>(dreamor::interval::closed(1, kLast)),
                                                   in<cItem>(urgent),
                                                   in<cMachine>(machines),
                                                   in<cItem>(items_of))
                          .where(fits_batch)
                          .build();
  static_assert(std::is_same_v<decltype(sWORST), decltype(sMAKE)>);
  DREAMOR_CHECK(sWORST.size() == 6 && sameRows(sWORST, sMAKE.where(sURGENT.membership())));

  // an of() named and built later, over named sources; its visit order read off its type
  const auto plan = of<cMachine, cItem>(in<cMachine>(machines), in<cItem>(items_of));
  DREAMOR_CHECK(sameRows(plan.build(), sROUTES) && sameRows(plan.where(fits_batch).build(), sROUTES.where(fits_batch)));
  static_assert(decltype(plan)::visitOrder() == "cMachine, cItem");

  // A callable passed as an rvalue is moved into the set, and an interval is held by value: neither is a temporary
  // container, so an of() named and built later over them is correct. Clang's -Wdangling, which -Werror makes an
  // error here, warned on it while a callable counted as a temporary container.
  const auto later = of<cMachine, cItem, cPeriod>(
      in<cMachine>(machines), in<cItem>(ItemsOf{}), in<cPeriod>(dreamor::interval::closed(1, kLast)));
  DREAMOR_CHECK(
      sameRows(later.build(), rel::from<cMachine>(machines).lateral<cItem>(ItemsOf{}).cross<cPeriod>(periods).build()));

  return report("set_builder");
} catch (...) { return ::uncaught(); }
