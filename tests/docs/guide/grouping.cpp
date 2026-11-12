// SPDX-License-Identifier: MIT-0
//
// docs/guide/grouping.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "docs/guide/model.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

// --8<-- [start:first-of-group-columns]
DREAMOR_VALUE_COLUMN(cSource, std::optional<std::string>);  // who supplies an item; none for an item the plant makes
DREAMOR_ENTITY_COLUMN(cLead, Item);                         // the first item of a lot
// --8<-- [end:first-of-group-columns]

using Produce = dreamor::IndexSet<cMachine, cItem, cPeriod>;

void grouping(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const Sets sets                                = byChain(p, bomTable(p));
  const Produce& sPRODUCE                        = sets.produce;
  const auto m1                                  = machines.at("M1");
  const auto m2                                  = machines.at("M2");

  // M1 makes the frame and the plate, M2 the plate, the bolt and the nut; the bracket is not needed. 5 pairs, in
  // 3 periods each: 15 rows, M1's first, each pair's three periods together.
  DREAMOR_CHECK(sPRODUCE.size() == 15);
  const std::vector<Produce::value_type> produceRows(sPRODUCE.begin(), sPRODUCE.end());
  DREAMOR_CHECK(produceRows[0] == Produce::value_type{m1, items.at("frame"), 1});
  DREAMOR_CHECK(produceRows[3] == Produce::value_type{m1, items.at("plate"), 1});
  DREAMOR_CHECK(produceRows[6] == Produce::value_type{m2, items.at("plate"), 1});
  DREAMOR_CHECK(produceRows[14] == Produce::value_type{m2, items.at("nut"), 3});

  // --8<-- [start:walk]
  std::vector<std::string> loads;  // how many items share a machine's hours in a period
  for (const auto& [m, t, rows] : sPRODUCE.groupBy<cMachine, cPeriod>()) {
    loads.push_back(std::format("{} in period {}: {} items", m, t, rows.size()));
  }
  // --8<-- [end:walk]
  // 2 machines in 3 periods, each with rows; the groups in the order their key's first row comes: M1's frame in
  // periods 1, 2 and 3 come first, so M1's three groups, then M2's.
  DREAMOR_CHECK(loads.size() == 6);
  DREAMOR_CHECK(loads[0] == "M1 in period 1: 2 items" && loads[1] == "M1 in period 2: 2 items" &&
                loads[2] == "M1 in period 3: 2 items");
  DREAMOR_CHECK(loads[3] == "M2 in period 1: 3 items" && loads[5] == "M2 in period 3: 3 items");

  // --8<-- [start:slice]
  const auto madeByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();

  const auto platesIn2   = madeByItemPeriod.slice(items.at("plate"), 2);    // on M1 and on M2
  const auto bracketsIn2 = madeByItemPeriod.slice(items.at("bracket"), 2);  // empty: the bracket is not needed
  const bool nutsIn3     = madeByItemPeriod.contains(items.at("nut"), 3);
  // --8<-- [end:slice]
  DREAMOR_CHECK(platesIn2.size() == 2);
  // in the set's order: M1's row comes before M2's
  DREAMOR_CHECK(std::get<cMachine>(platesIn2[0]) == m1 && std::get<cMachine>(platesIn2[1]) == m2);
  DREAMOR_CHECK(std::get<cItem>(platesIn2[0]) == items.at("plate") && std::get<cPeriod>(platesIn2[1]) == 2);
  DREAMOR_CHECK(bracketsIn2.empty());
  DREAMOR_CHECK(nutsIn3);
  DREAMOR_CHECK(!madeByItemPeriod.contains(items.at("bracket"), 2));
  // a group for each item made and each period: frame, plate, bolt and nut in 3 periods
  DREAMOR_CHECK(madeByItemPeriod.size() == 12);

  // --8<-- [start:at]
  const auto byMachinePeriod = sPRODUCE.groupBy<cMachine, cPeriod>();

  const auto last               = byMachinePeriod.at(byMachinePeriod.size() - 1);
  const auto& [machine, period] = last.key;
  const std::size_t itemsMade   = last.rows.size();
  // --8<-- [end:at]
  DREAMOR_CHECK(byMachinePeriod.size() == 6);  // 2 machines in 3 periods, as the walk above
  static_assert(std::is_same_v<decltype(last.rows), dreamor::RowSlice<Produce::value_type>>);
  DREAMOR_CHECK(machine == m2 && period == 3 && itemsMade == 3);  // M2 in period 3: plate, bolt, nut
  // A group's rows are the set's own: M2's plate in period 3 is row 8 of sPRODUCE.
  DREAMOR_CHECK(&last.rows.front() == &*(sPRODUCE.begin() + 8));
  DREAMOR_CHECK_THROWS(std::out_of_range, byMachinePeriod.at(byMachinePeriod.size()));

  // --8<-- [start:regrouped]
  const auto sBY_PERIOD = sPRODUCE.groupBy<cPeriod>().regrouped();  // period 1's rows, then 2's, then 3's
  // --8<-- [end:regrouped]
  static_assert(std::is_same_v<decltype(sBY_PERIOD), const Produce>);
  DREAMOR_CHECK(sBY_PERIOD == sPRODUCE);  // the same rows, in another order
  const std::vector<Produce::value_type> regrouped(sBY_PERIOD.begin(), sBY_PERIOD.end());
  // 5 rows a period; within a period, the set's order: M1's frame and plate, M2's plate, bolt and nut
  DREAMOR_CHECK(regrouped[0] == Produce::value_type{m1, items.at("frame"), 1});
  DREAMOR_CHECK(regrouped[1] == Produce::value_type{m1, items.at("plate"), 1});
  DREAMOR_CHECK(regrouped[4] == Produce::value_type{m2, items.at("nut"), 1});
  DREAMOR_CHECK(regrouped[5] == Produce::value_type{m1, items.at("frame"), 2});
  DREAMOR_CHECK(regrouped[14] == Produce::value_type{m2, items.at("nut"), 3});
}

void firstOfGroup(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;

  // --8<-- [start:first-of-group]
  using dreamor::index_set::relational::from;

  const std::map<std::string, std::string> suppliers = {{"bolt", "Acme"}, {"nut", "Acme"}};  // the others are made
  const auto with_source                             = [&suppliers](cItem i) {
    const auto found = suppliers.find(i->name);
    return std::tuple<cItem, cSource>{i, found == suppliers.end() ? cSource{} : cSource{found->second}};
  };
  const auto sSOURCED = from<cItem>(items).build().select(with_source);  // each item with its supplier, or none
  const auto lots     = sSOURCED.groupBy<cSource>();                     // a lot: the items of one supplier

  const auto lead_of = [](const auto& lot) {
    return std::tuple<cLead, cSource>{cLead{std::get<cItem>(lot.rows.front())}, std::get<cSource>(lot.key)};
  };
  const auto with_lead = [&lots](cItem i, const cSource& s) {
    return std::tuple<cLead, cItem>{cLead{std::get<cItem>(lots.slice(s).front())}, i};
  };
  const dreamor::IndexSet<cLead, cSource> sLEADS{lots, lead_of};  // the first item of each lot
  const auto sLOTS = sSOURCED.select(with_lead);                  // each item with its lot's first item

  const auto itemsOfLot            = sLOTS.groupBy<cLead>();
  const std::size_t boughtTogether = itemsOfLot.slice(items.at("bolt")).size();
  const std::string firstLot       = dreamor::indexedName("Lot", std::get<cLead>(*sLEADS.begin()));
  // --8<-- [end:first-of-group]
  // The items are sorted by name: bolt, bracket, frame, nut, plate. The bolt and the nut come from Acme; the bracket,
  // the frame and the plate have no supplier, and two missing suppliers are equal, so they make one lot.
  const auto bolt    = items.at("bolt");
  const auto bracket = items.at("bracket");
  DREAMOR_CHECK(sSOURCED.size() == 5 && sSOURCED.contains(items.at("frame"), cSource{}) &&
                sSOURCED.contains(items.at("nut"), cSource{std::string{"Acme"}}));
  DREAMOR_CHECK(lots.size() == 2);
  DREAMOR_CHECK(sLEADS.size() == 2 && std::get<cLead>(sLEADS.begin()[0]) == bolt &&
                std::get<cLead>(sLEADS.begin()[1]) == bracket);  // the lots in the order their first items come
  DREAMOR_CHECK(sLOTS.size() == 5);                              // every item, in the order of sSOURCED
  // Both columns of sLOTS hold an Item, so a key names each value's column.
  DREAMOR_CHECK(sLOTS.contains(cLead{bolt}, cItem{bolt}) && sLOTS.contains(cLead{bolt}, cItem{items.at("nut")}));
  DREAMOR_CHECK(sLOTS.contains(cLead{bracket}, cItem{bracket}) &&
                sLOTS.contains(cLead{bracket}, cItem{items.at("frame")}) &&
                sLOTS.contains(cLead{bracket}, cItem{items.at("plate")}));
  DREAMOR_CHECK(std::get<cItem>(sLOTS.begin()[3]) == items.at("nut") && std::get<cLead>(sLOTS.begin()[3]) == bolt);
  DREAMOR_CHECK(boughtTogether == 2 && itemsOfLot.slice(bracket).size() == 3);
  DREAMOR_CHECK(firstLot == "Lot_bolt");
}

}  // namespace

int main() try {
  const Plant p = plant();
  grouping(p);
  firstOfGroup(p);
  return ::report("guide/grouping");
} catch (...) { return ::uncaught(); }
