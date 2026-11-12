// SPDX-License-Identifier: MIT-0
//
// docs/guide/values-over-a-set.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <cstddef>
#include <format>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "docs/guide/model.hpp"
#include "docs/log_capture.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;

void valuesOverASet(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const int periods                              = p.periods;  // 3
  const cMachine m1                              = machines.at("M1");
  const cMachine m2                              = machines.at("M2");

  // --8<-- [start:sets]
  using dreamor::index_set::relational::from;

  const auto items_of = [](cMachine m) -> const auto& { return m->items; };

  // each machine and each item it makes
  const auto sMAKE = from<cMachine>(machines).lateral<cItem>(items_of).build();
  // each item in each period
  const auto sDEMAND = from<cItem>(items).cross<cPeriod>(dreamor::interval::closed(1, periods)).build();
  // --8<-- [end:sets]
  static_assert(std::is_same_v<decltype(sMAKE), const dreamor::IndexSet<cMachine, cItem>>);
  static_assert(std::is_same_v<decltype(sDEMAND), const dreamor::IndexSet<cItem, cPeriod>>);
  DREAMOR_CHECK(sMAKE.size() == 6);     // 3 items on each of 2 machines
  DREAMOR_CHECK(sDEMAND.size() == 15);  // 5 items in 3 periods

  const LogCapture log;
  DREAMOR_CHECK(log.isOpen());

  // --8<-- [start:all-any-none]
  const auto fits_a_period = [](cMachine m, cItem i) { return i->time <= m->capacity; };      // a unit in a period
  const auto fits_batch    = [](cMachine m, cItem i) { return 8 * i->time <= m->capacity; };  // a batch of 8 units
  const auto is_rush       = [](cItem i, cPeriod t) { return i->demand[t - 1] > 10; };        // over 10 units at once

  const bool unitsFit  = dreamor::allOf(sMAKE, fits_a_period);  // a unit of each item fits its machine's period
  const bool someBatch = dreamor::anyOf(sMAKE, fits_batch);     // a batch of 8 fits for some item
  const bool noRush    = dreamor::noneOf(sDEMAND, is_rush);     // no period asks over 10 units
  if (!unitsFit) { DREAMOR_LOG_ERROR("an item takes more hours than its machine has in a period"); }
  // --8<-- [end:all-any-none]
  // The slowest unit is the frame's hour on M1 (4 hours) and the plate's half hour on M2 (3 hours): each fits.
  DREAMOR_CHECK(unitsFit);
  DREAMOR_CHECK(log.text().empty());  // so no error line is written
  // M1 fits 8 plates in its 4 hours (8 * 0.5), not 8 frames (8 * 1.0).
  DREAMOR_CHECK(someBatch);
  DREAMOR_CHECK(fits_batch(m1, cItem{items.at("plate")}) && !fits_batch(m1, cItem{items.at("frame")}));
  DREAMOR_CHECK(noRush);  // the demands are 0, 1, 2 and 5

  // --8<-- [start:count]
  const auto has_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };

  const std::size_t sales = dreamor::countIf(sDEMAND, has_demand);  // the rows customers buy in
  // --8<-- [end:count]
  DREAMOR_CHECK(sales == 4);  // the frame in periods 1, 2 and 3, the nut in period 2

  // --8<-- [start:max-min-sum]
  const auto demand_of  = [](cItem i, cPeriod t) { return i->demand[t - 1]; };
  const auto unit_hours = [](cItem i) { return i->time; };

  const double peak    = dreamor::maxOf(sDEMAND, demand_of);     // the largest demand in a period
  const double fastest = dreamor::minOf(sMAKE, unit_hours);      // the fewest hours a unit takes
  const double total   = dreamor::sumValue(sDEMAND, demand_of);  // the total demand, a number
  // --8<-- [end:max-min-sum]
  DREAMOR_CHECK(exactlyEqual(peak, 5.0));     // 5 nuts in period 2
  DREAMOR_CHECK(exactlyEqual(fastest, 0.1));  // a bolt or a nut on M2
  DREAMOR_CHECK(exactlyEqual(total, 10.0));   // frames 1 + 2 + 2, nuts 5

  // --8<-- [start:group]
  const auto byMachine     = sMAKE.groupBy<cMachine>();
  const double slowestOnM2 = dreamor::maxOf(byMachine.slice(machines.at("M2")), unit_hours);
  // --8<-- [end:group]
  std::vector<cItem> onM2;
  for (const auto& [m, i] : byMachine.slice(m2)) {
    onM2.push_back(i);
  }
  DREAMOR_CHECK(onM2 == std::vector<cItem>{items.at("plate"), items.at("bolt"), items.at("nut")});
  DREAMOR_CHECK(exactlyEqual(slowestOnM2, 0.5));  // the plate; the frame's hour is M1's

  // --8<-- [start:find]
  const auto* peakRow  = dreamor::maxElement(sDEMAND, demand_of);  // the row of the largest demand
  const auto* quickRow = dreamor::minElement(sMAKE, unit_hours);   // the row of the quickest unit
  const auto* firstRow = dreamor::findIf(sDEMAND, has_demand);     // the first row customers buy in
  const auto* rushRow  = dreamor::findIf(sDEMAND, is_rush);        // nullptr: no period asks over 10 units

  const auto& [item, period] = *peakRow;  // sDEMAND has rows, so peakRow points at one
  const std::string peakText = std::format("{} in period {}", item, period);
  // --8<-- [end:find]
  static_assert(std::is_same_v<decltype(peakRow), const std::tuple<cItem, cPeriod>*>);
  static_assert(std::is_same_v<decltype(quickRow), const std::tuple<cMachine, cItem>*>);
  // Each points at a row the set holds, not a copy.
  const auto rowOf = [](const auto& set, const auto& row) {
    for (const auto& held : set) {
      if (held == row) { return &held; }
    }
    return static_cast<decltype(&*set.begin())>(nullptr);
  };
  DREAMOR_CHECK(peakRow == rowOf(sDEMAND, std::tuple{cItem{items.at("nut")}, cPeriod{2}}));
  DREAMOR_CHECK(peakText == "nut in period 2");
  // M2's bolt and nut both take 0.1 hours; M2 makes the bolt first.
  DREAMOR_CHECK(quickRow == rowOf(sMAKE, std::tuple{m2, cItem{items.at("bolt")}}));
  // The items in key order are bolt, bracket, frame, nut, plate: the frame's period 1 is the first row in demand.
  DREAMOR_CHECK(firstRow == rowOf(sDEMAND, std::tuple{cItem{items.at("frame")}, cPeriod{1}}));
  DREAMOR_CHECK(rushRow == nullptr);

  // --8<-- [start:plain-container]
  const auto capacity_of = [](cMachine m) { return m->capacity; };

  const double plantHours = dreamor::sumValue<cMachine>(machines, capacity_of);  // the machines' hours in a period
  const double m1Units    = dreamor::sumValue<cItem>(machines.at("M1")->items, unit_hours);  // a unit of each M1 item
  // --8<-- [end:plain-container]
  DREAMOR_CHECK(exactlyEqual(plantHours, 7.0));  // M1's 4 and M2's 3
  DREAMOR_CHECK(exactlyEqual(m1Units, 2.0));     // a frame 1, a plate 0.5, a bracket 0.5
}

}  // namespace

int main() try {
  const Plant p = plant();
  valuesOverASet(p);
  return ::report("guide/values-over-a-set");
} catch (...) { return ::uncaught(); }
