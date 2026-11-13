// SPDX-License-Identifier: MIT-0
//
// docs/guide/data-on-a-set.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "docs/guide/model.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;

// --8<-- [start:route]
struct Route {   // making an item on a machine
  double cost;   // a unit's running cost
  double scrap;  // the share of units scrapped
};

struct RouteRecord {
  std::string machine, item;
  double cost, scrap;
};

// --8<-- [end:route]

void onEachRow(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const int periods                              = p.periods;  // 3

  // --8<-- [start:on-each-row]
  using dreamor::index_set::relational::from;
  const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, periods)).build();
  const auto sPLAN    = from<cItem>(items).cross(sPERIODS).build();        // each item in each period
  const auto sSHIFTS  = from<cMachine>(machines).cross(sPERIODS).build();  // each machine in each period

  const auto demand_of = [](cItem i, cPeriod t) { return i->demand[t - 1]; };
  const dreamor::ParamTable<double, cItem, cPeriod> demand{sPLAN, demand_of};
  const dreamor::ParamTable<double, cMachine, cPeriod> overtime{sSHIFTS, 2.0};  // hours a machine may add
  // --8<-- [end:on-each-row]
  const auto frame = items.at("frame");
  const auto nut   = items.at("nut");
  DREAMOR_CHECK(sPLAN.size() == 15 && demand.size() == 15);  // 5 items in 3 periods
  DREAMOR_CHECK(exactlyEqual(demand(frame, 1), 1) && exactlyEqual(demand(frame, 3), 2) &&
                exactlyEqual(demand(nut, 2), 5));
  const auto bracket = items.at("bracket");  // a product no longer sold: 0 in every period
  DREAMOR_CHECK(isExactlyZero(demand(bracket, 1)) && isExactlyZero(demand(bracket, 2)) &&
                isExactlyZero(demand(bracket, 3)));
  DREAMOR_CHECK(sSHIFTS.size() == 6 && overtime.size() == 6);  // 2 machines in 3 periods
  std::size_t twoHours = 0;
  for (const auto& [row, hours] : overtime) {
    if (exactlyEqual(hours, 2.0)) { ++twoHours; }
  }
  DREAMOR_CHECK(twoHours == 6);

  // --8<-- [start:hand-on]
  const auto has_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };
  const auto sSALES     = from(sPLAN).where(has_demand).build();  // the rows customers buy

  const dreamor::ParamTable<double, cItem, cPeriod> orders{sSALES, demand};  // demand's value on each
  const double unitsSold = dreamor::sumValue(sSALES, demand);
  // --8<-- [end:hand-on]
  DREAMOR_CHECK(sSALES.size() == 4 && orders.size() == 4);  // the frame in periods 1, 2 and 3, the nut in 2
  DREAMOR_CHECK(exactlyEqual(orders(frame, 1), 1) && exactlyEqual(orders(frame, 2), 2) &&
                exactlyEqual(orders(frame, 3), 2) && exactlyEqual(orders(nut, 2), 5));
  DREAMOR_CHECK(!orders.contains(nut, 1) && !orders.contains(nut, 3));
  DREAMOR_CHECK(!orders.contains(bracket, 1) && !orders.contains(bracket, 2) && !orders.contains(bracket, 3));
  DREAMOR_CHECK(exactlyEqual(unitsSold, 10));  // the frame's 1 + 2 + 2 and the nut's 5
}

// The tables of onEachRow with their types left to the compiler, and two more: a whole number deduces int, a string
// literal std::string_view.
void deduced(const Plant& p) {
  using dreamor::index_set::relational::from;
  const auto sPERIODS  = from<cPeriod>(dreamor::interval::closed(1, p.periods)).build();
  const auto sPLAN     = from<cItem>(p.items).cross(sPERIODS).build();
  const auto sSHIFTS   = from<cMachine>(p.machines).cross(sPERIODS).build();
  const auto demand_of = [](cItem i, cPeriod t) { return i->demand[t - 1]; };

  // --8<-- [start:deduced]
  const dreamor::ParamTable demand{sPLAN, demand_of};  // ParamTable<double, cItem, cPeriod>
  const dreamor::ParamTable overtime{sSHIFTS, 2.0};    // ParamTable<double, cMachine, cPeriod>
  const dreamor::ParamTable crew{sSHIFTS, 2};          // ParamTable<int, cMachine, cPeriod>: 2 is an int
  const dreamor::ParamTable shift{sSHIFTS, "day"};     // ParamTable<std::string_view, cMachine, cPeriod>
  // --8<-- [end:deduced]
  static_assert(std::is_same_v<decltype(demand), const dreamor::ParamTable<double, cItem, cPeriod>>);
  static_assert(std::is_same_v<decltype(overtime), const dreamor::ParamTable<double, cMachine, cPeriod>>);
  static_assert(std::is_same_v<decltype(crew), const dreamor::ParamTable<int, cMachine, cPeriod>>);
  static_assert(std::is_same_v<decltype(shift), const dreamor::ParamTable<std::string_view, cMachine, cPeriod>>);
  const auto frame = p.items.at("frame");
  const auto m1    = p.machines.at("M1");
  DREAMOR_CHECK(demand.size() == 15 && exactlyEqual(demand(frame, 3), 2) && exactlyEqual(overtime(m1, 2), 2.0));
  DREAMOR_CHECK(crew(m1, 1) == 2 && shift(m1, 3) == "day" && shift.size() == 6);
}

void bom(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;

  // --8<-- [start:bom-keys]
  const BomTable quantity                        = bomTable(p);
  const dreamor::IndexSet<cParent, cChild>& sBOM = quantity.keys();  // a row for each line of the BOM
  // --8<-- [end:bom-keys]
  DREAMOR_CHECK(quantity.size() == 6 && sBOM.size() == 6);  // bom-data's six lines
  // The rows and values in the records' order, each as its record names it.
  std::size_t k       = 0;
  bool followsRecords = true;
  for (const auto& [row, value] : quantity) {
    const auto& [parent, child] = row;
    followsRecords              = followsRecords && parent->name == p.bom[k].parent && child->name == p.bom[k].child &&
                                  exactlyEqual(value, p.bom[k].quantity);
    ++k;
  }
  DREAMOR_CHECK(followsRecords && k == 6);

  // --8<-- [start:read]
  const auto frame = items.at("frame");
  const auto plate = items.at("plate");

  const double plates = quantity(cParent{frame}, cChild{plate});                     // a frame takes 2 plates
  const bool takesNut = quantity.contains(cParent{frame}, cChild{items.at("nut")});  // false: its plates do

  const std::tuple<cParent, cChild> line{frame, plate};
  const double same = quantity(line);  // a row of the set reads as its key
  // --8<-- [end:read]
  DREAMOR_CHECK(exactlyEqual(plates, 2) && !takesNut && exactlyEqual(same, 2));
  DREAMOR_CHECK(quantity.contains(cParent{plate}, cChild{items.at("nut")}));
  DREAMOR_CHECK_THROWS(std::out_of_range, quantity(cParent{plate}, cChild{frame}));  // no line has a plate take one

  // --8<-- [start:walk]
  std::vector<std::string> lines;
  for (const auto& [row, units] : quantity) {
    const auto& [parent, child] = row;
    lines.push_back(std::format("{} takes {} {}", parent, units, child));
  }
  // --8<-- [end:walk]
  DREAMOR_CHECK(lines.size() == 6);
  DREAMOR_CHECK(lines[0] == "frame takes 2 plate" && lines[1] == "frame takes 4 bolt" &&
                lines[3] == "plate takes 2 nut" && lines[5] == "bracket takes 2 bolt");
}

void severalValues(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;

  // --8<-- [start:several-values]
  const std::vector<RouteRecord> routes{
      {"M1", "frame", 6.0, 0.01},
      {"M1", "plate", 3.0, 0.02},
      {"M2", "plate", 2.5, 0.05},
  };
  const auto route_key = [&machines, &items](const RouteRecord& r) {
    return std::tuple<cMachine, cItem>{machines.at(r.machine), items.at(r.item)};
  };
  const auto route_of = [](const RouteRecord& r) { return Route{r.cost, r.scrap}; };
  const dreamor::ParamTable<Route, cMachine, cItem> route{routes, route_key, route_of};

  const Route& plateOnM2 = route(machines.at("M2"), items.at("plate"));  // cheaper than on M1, more scrap
  // --8<-- [end:several-values]
  DREAMOR_CHECK(route.size() == 3);
  DREAMOR_CHECK(exactlyEqual(plateOnM2.cost, 2.5) && exactlyEqual(plateOnM2.scrap, 0.05));
  const Route& plateOnM1 = route(machines.at("M1"), items.at("plate"));
  DREAMOR_CHECK(exactlyEqual(plateOnM1.cost, 3.0) && exactlyEqual(plateOnM1.scrap, 0.02));
  DREAMOR_CHECK(plateOnM2.cost < plateOnM1.cost && plateOnM2.scrap > plateOnM1.scrap);
  DREAMOR_CHECK(!route.contains(machines.at("M2"), items.at("frame")));
}

}  // namespace

int main() try {
  const Plant p = plant();
  onEachRow(p);
  deduced(p);
  bom(p);
  severalValues(p);
  return ::report("guide/data-on-a-set");
} catch (...) { return ::uncaught(); }
