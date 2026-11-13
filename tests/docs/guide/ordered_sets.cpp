// SPDX-License-Identifier: MIT-0
//
// docs/guide/ordered-sets.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <cstddef>
#include <optional>
#include <tuple>
#include <vector>

#include "docs/guide/model.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

// --8<-- [start:member]
// The machines' items and their order on each machine, kept together.
struct Routing {
  dreamor::IndexSet<cMachine, cItem> sMADE;                                       // no rows until assigned
  dreamor::Ordered<dreamor::IndexSet<cMachine, cItem>, cMachine> itemsOnMachine;  // no rows until assigned
};

Routing routingOf(const dreamor::EntityVector<Machine>& machines) {
  using dreamor::index_set::relational::from;
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };

  Routing routing;
  routing.sMADE          = from<cMachine>(machines).lateral<cItem>(items_of).build();
  routing.itemsOnMachine = routing.sMADE.orderedWithin<cMachine>();
  return routing;
}

// --8<-- [end:member]

void member(const Plant& p) {
  const Routing empty;
  DREAMOR_CHECK(empty.sMADE.empty() && empty.itemsOnMachine.empty());
  const Routing routing = routingOf(p.machines);
  const cMachine m1     = p.machines.at("M1");
  DREAMOR_CHECK(routing.sMADE.size() == 6 && routing.itemsOnMachine.size() == 6);
  DREAMOR_CHECK(routing.itemsOnMachine.first(m1) == std::tuple<cMachine, cItem>{m1, p.items.at("frame")});
}

void orderedSets(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const int periods                              = p.periods;  // 3
  const Sets s                                   = byChain(p, bomTable(p));

  // --8<-- [start:positions]
  const auto orderedPeriods = s.periods.ordered();

  const std::optional<std::size_t> position = orderedPeriods.positionOf(2);  // 1: positions count from 0
  const auto& [opening]                     = orderedPeriods.first();        // period 1
  const auto& [closing]                     = orderedPeriods.last();         // period 3
  const bool opensAtOne                     = orderedPeriods.isFirst(1);     // true
  const bool closesAtTwo                    = orderedPeriods.isLast(2);      // false
  // --8<-- [end:positions]
  // The model's interval makes the periods 1, 2, 3, in that order.
  DREAMOR_CHECK(orderedPeriods.size() == 3 && orderedPeriods.at(0) == std::tuple<cPeriod>{1} &&
                orderedPeriods.at(1) == std::tuple<cPeriod>{2} && orderedPeriods.at(2) == std::tuple<cPeriod>{3});
  DREAMOR_CHECK(position == std::optional<std::size_t>{1} &&
                orderedPeriods.positionOf(1) == std::optional<std::size_t>{0});
  DREAMOR_CHECK(opening == 1 && closing == 3);
  DREAMOR_CHECK(opensAtOne && !closesAtTwo && orderedPeriods.isLast(3));
  DREAMOR_CHECK(orderedPeriods.positionOf(4) == std::nullopt);  // not a period of the set

  // --8<-- [start:prev]
  const auto frame = items.at("frame");
  std::vector<double> soldBy(periods);  // the frames sold up to period t, at [t - 1]
  for (const auto& [t] : s.periods) {
    soldBy[t - 1] = frame->demand[t - 1];
    if (const auto* before = orderedPeriods.prev(t)) {
      const auto& [earlier] = *before;
      soldBy[t - 1] += soldBy[earlier - 1];
    }
  }
  // --8<-- [end:prev]
  // The frame's demand is 1, 2, 2: period 1 has no period before and keeps its own 1; 1 + 2 = 3; 3 + 2 = 5.
  DREAMOR_CHECK(soldBy == std::vector<double>{1, 3, 5});
  DREAMOR_CHECK(orderedPeriods.prev(1) == nullptr && orderedPeriods.next(3) == nullptr);
  DREAMOR_CHECK(orderedPeriods.prev(2) == &orderedPeriods.at(0));  // a pointer to the row itself

  // --8<-- [start:where-neighbour]
  const auto not_first = [&orderedPeriods](cPeriod t) { return !orderedPeriods.isFirst(t); };

  std::vector<std::tuple<cItem, cPeriod, double>> changes;  // the change in demand from the period before
  for (const auto& [i, t] : s.stock.where(not_first)) {     // each item kept, in every period but the first
    const auto& [earlier] = orderedPeriods.prevOf(t);
    changes.emplace_back(i, t, i->demand[t - 1] - i->demand[earlier - 1]);
  }
  // --8<-- [end:where-neighbour]
  DREAMOR_CHECK(s.stock.size() == 12);  // the 4 needed items - frame, nut, plate, bolt - in 3 periods each
  DREAMOR_CHECK(changes.size() == 8);   // the 4 items in periods 2 and 3, frame first
  DREAMOR_CHECK(changes.front() == std::tuple<cItem, cPeriod, double>{items.at("frame"), cPeriod{2}, 1.0});
  const auto has_change = [&changes](const std::tuple<cItem, cPeriod, double>& change) {
    return std::ranges::find(changes, change) != changes.end();
  };
  // frame: 1 -> 2 -> 2; nut: 0 -> 5 -> 0.
  DREAMOR_CHECK(has_change({items.at("frame"), cPeriod{2}, 1.0}) && has_change({items.at("frame"), cPeriod{3}, 0.0}));
  DREAMOR_CHECK(has_change({items.at("nut"), cPeriod{2}, 5.0}) && has_change({items.at("nut"), cPeriod{3}, -5.0}));
  // plate and bolt: no demand in any period.
  DREAMOR_CHECK(has_change({items.at("plate"), cPeriod{2}, 0.0}) && has_change({items.at("plate"), cPeriod{3}, 0.0}));
  DREAMOR_CHECK(has_change({items.at("bolt"), cPeriod{2}, 0.0}) && has_change({items.at("bolt"), cPeriod{3}, 0.0}));
  const auto in_first_period = [](const std::tuple<cItem, cPeriod, double>& change) {
    return std::get<cPeriod>(change) == 1;
  };
  DREAMOR_CHECK(std::ranges::none_of(changes, in_first_period));
  // prevOf hands out the row itself, the one prev points to.
  DREAMOR_CHECK(&orderedPeriods.prevOf(3) == orderedPeriods.prev(3));

  // --8<-- [start:cyclic]
  std::vector<double> soldBefore(periods);  // the frames sold the period before t; the horizon repeats
  for (const auto& [t] : s.periods) {
    const auto& [earlier] = *orderedPeriods.prevCyclic(t);  // before period 1, period 3
    soldBefore[t - 1]     = frame->demand[earlier - 1];
  }
  // --8<-- [end:cyclic]
  // The frame's demand is 1, 2, 2: before period 1 comes period 3 (2), then period 1 (1), then period 2 (2).
  DREAMOR_CHECK(soldBefore == std::vector<double>{2, 1, 2});
  DREAMOR_CHECK(*orderedPeriods.nextCyclic(3) == std::tuple<cPeriod>{1});  // past the last, the first
  DREAMOR_CHECK(orderedPeriods.prevCyclic(4) == nullptr);                  // not a period of the set

  // --8<-- [start:within]
  using dreamor::index_set::relational::from;

  const auto items_of = [](cMachine m) -> const auto& { return m->items; };

  const auto sMADE          = from<cMachine>(machines).lateral<cItem>(items_of).build();  // each machine's items
  const auto itemsOnMachine = sMADE.orderedWithin<cMachine>();

  const cMachine m1        = machines.at("M1");
  const auto& firstOnM1    = itemsOnMachine.first(m1);                                   // M1's frame
  const auto* afterPlate   = itemsOnMachine.next(m1, items.at("plate"));                 // M1's bracket
  const auto* afterBracket = itemsOnMachine.next(m1, items.at("bracket"));               // nullptr: M1's last
  const bool nutLastOnM2   = itemsOnMachine.isLast(machines.at("M2"), items.at("nut"));  // true
  // --8<-- [end:within]
  // M1 lists frame, plate, bracket; M2 plate, bolt, nut: the set holds them in that order, M1 first.
  DREAMOR_CHECK(sMADE.size() == 6 && sMADE.begin()[0] == std::tuple<cMachine, cItem>{m1, items.at("frame")} &&
                sMADE.begin()[1] == std::tuple<cMachine, cItem>{m1, items.at("plate")} &&
                sMADE.begin()[2] == std::tuple<cMachine, cItem>{m1, items.at("bracket")} &&
                sMADE.begin()[3] == std::tuple<cMachine, cItem>{machines.at("M2"), items.at("plate")});
  DREAMOR_CHECK(firstOnM1 == std::tuple<cMachine, cItem>{m1, items.at("frame")});
  DREAMOR_CHECK(afterPlate != nullptr && *afterPlate == std::tuple<cMachine, cItem>{m1, items.at("bracket")});
  // The walk stops at M1's end: the row after M1's bracket in the set, M2's plate, is another machine's.
  DREAMOR_CHECK(afterBracket == nullptr);
  DREAMOR_CHECK(nutLastOnM2 && !itemsOnMachine.isLast(m1, items.at("plate")));
  // Positions count within the group: M2's nut is its third item; the plate is M1's second and M2's first.
  DREAMOR_CHECK(itemsOnMachine.positionOf(machines.at("M2"), items.at("nut")) == std::optional<std::size_t>{2});
  DREAMOR_CHECK(itemsOnMachine.positionOf(m1, items.at("plate")) == std::optional<std::size_t>{1} &&
                itemsOnMachine.positionOf(machines.at("M2"), items.at("plate")) == std::optional<std::size_t>{0});
  DREAMOR_CHECK(itemsOnMachine.last(machines.at("M2")) ==
                std::tuple<cMachine, cItem>{machines.at("M2"), items.at("nut")});
}

}  // namespace

int main() try {
  const Plant p = plant();
  orderedSets(p);
  member(p);
  return ::report("guide/ordered-sets");
} catch (...) { return ::uncaught(); }
