// SPDX-License-Identifier: MIT-0
//
// docs/guide/deriving-sets.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <format>
#include <string>
#include <tuple>
#include <type_traits>

#include "docs/guide/model.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

// A set's rows written out in its order, a row's columns joined by a space and the rows by a comma: read off the
// set itself, to hold the page's lists of rows to the set's order.
template <typename... Columns>
std::string rowsOf(const dreamor::IndexSet<Columns...>& set) {
  std::string out;
  for (const auto& row : set) {
    if (!out.empty()) { out += ", "; }
    std::apply([&out](const auto&... column) { ((out += std::format("{} ", column)), ...); }, row);
    out.pop_back();
  }
  return out;
}

void derivingSets(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const int periods                              = p.periods;  // 3
  const BomTable quantity                        = bomTable(p);

  // --8<-- [start:make]
  using dreamor::index_set::relational::from;

  const auto items_of = [](cMachine m) -> const auto& { return m->items; };

  const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, periods)).build();
  const auto sMAKE    = from<cMachine>(machines).lateral<cItem>(items_of).cross(sPERIODS).build();
  // --8<-- [end:make]
  static_assert(std::is_same_v<decltype(sMAKE), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  DREAMOR_CHECK(sMAKE.size() == 18);  // 3 items on each of 2 machines, in 3 periods
  // Each machine's items in the order the machine lists them, each item's three periods together.
  DREAMOR_CHECK(rowsOf(sMAKE).starts_with("M1 frame 1, M1 frame 2, M1 frame 3, M1 plate 1"));
  DREAMOR_CHECK(rowsOf(sMAKE).ends_with("M2 nut 3"));

  // --8<-- [start:where]
  const auto fits_batch  = [](cMachine m, cItem i) { return 8 * i->time <= m->capacity; };  // a batch of 8 units
  const auto after_first = [](cPeriod t) { return t > 1; };

  const auto sBATCH = sMAKE.where(fits_batch, after_first);
  // --8<-- [end:where]
  static_assert(std::is_same_v<decltype(sBATCH), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  // A batch fits M1's plates and brackets (4 of its 4 hours) and M2's bolts and nuts (0.8 of 3), not M1's frames
  // (8 hours) nor M2's plates (4 hours): 4 pairs, in periods 2 and 3.
  DREAMOR_CHECK(sBATCH.size() == 8);
  DREAMOR_CHECK(rowsOf(sBATCH) ==
                "M1 plate 2, M1 plate 3, M1 bracket 2, M1 bracket 3, M2 bolt 2, M2 bolt 3, M2 nut 2, M2 nut 3");

  // --8<-- [start:select-columns]
  const auto sROUTES = sMAKE.select<cMachine, cItem>();  // each machine and an item it makes
  const auto sMADE   = sMAKE.select<cItem>();            // each item a machine makes
  // --8<-- [end:select-columns]
  static_assert(std::is_same_v<decltype(sROUTES), const dreamor::IndexSet<cMachine, cItem>>);
  static_assert(std::is_same_v<decltype(sMADE), const dreamor::IndexSet<cItem>>);
  DREAMOR_CHECK(sROUTES.size() == 6);  // 3 items on each of 2 machines, each pair once for its 3 periods
  DREAMOR_CHECK(rowsOf(sROUTES) == "M1 frame, M1 plate, M1 bracket, M2 plate, M2 bolt, M2 nut");
  DREAMOR_CHECK(sMADE.size() == 5);  // the 6 pairs' items, the plate in two of them
  DREAMOR_CHECK(rowsOf(sMADE) == "frame, plate, bracket, bolt, nut");

  // --8<-- [start:select-expr]
  const auto as_parent   = [](cItem i) { return cParent{i}; };
  const auto next_period = [](cItem i, cPeriod t) { return std::tuple<cItem, cPeriod>{i, t + 1}; };

  const auto sPARENTS_made = sMAKE.select(as_parent);    // each item made, as a parent in the bill of materials
  const auto sNEXT         = sMAKE.select(next_period);  // each item made in a period, in the period after
  // --8<-- [end:select-expr]
  static_assert(std::is_same_v<decltype(sPARENTS_made), const dreamor::IndexSet<cParent>>);
  static_assert(std::is_same_v<decltype(sNEXT), const dreamor::IndexSet<cItem, cPeriod>>);
  DREAMOR_CHECK(rowsOf(sPARENTS_made) == "frame, plate, bracket, bolt, nut");
  // As a cParent it meets the bill of materials' lines on their parent: every line's parent is made, so all 6 match.
  DREAMOR_CHECK(from(quantity.keys()).join(sPARENTS_made).build().size() == 6);
  DREAMOR_CHECK(sNEXT.size() == 15);  // 5 items in periods 2 to 4: M2's plate repeats M1's
  DREAMOR_CHECK(rowsOf(sNEXT) ==
                "frame 2, frame 3, frame 4, plate 2, plate 3, plate 4, bracket 2, bracket 3, bracket 4, "
                "bolt 2, bolt 3, bolt 4, nut 2, nut 3, nut 4");

  // --8<-- [start:order-by]
  const auto time_of         = [](cItem i) { return i->time; };
  const auto item_then_maker = [](cMachine m, cItem i) { return std::tuple{i, m}; };

  const auto sMADE_quick_first = sMADE.orderBy(time_of);
  const auto sMADE_slow_first  = sMADE.orderBy(time_of, dreamor::descending);
  const auto sROUTES_by_item   = sROUTES.orderBy(item_then_maker);
  // --8<-- [end:order-by]
  static_assert(std::is_same_v<decltype(sROUTES_by_item), const dreamor::IndexSet<cMachine, cItem>>);
  // Hours a unit: frame 1.0, plate and bracket 0.5, bolt and nut 0.1; ties keep sMADE's order.
  DREAMOR_CHECK(rowsOf(sMADE_quick_first) == "bolt, nut, plate, bracket, frame");
  DREAMOR_CHECK(rowsOf(sMADE_slow_first) == "frame, plate, bracket, bolt, nut");
  // The items by name, and the plate's two machines by theirs.
  DREAMOR_CHECK(rowsOf(sROUTES_by_item) == "M2 bolt, M1 bracket, M1 frame, M2 nut, M1 plate, M2 plate");

  // --8<-- [start:contains]
  const bool m2_makes_bolts = sROUTES.contains(machines.at("M2"), items.at("bolt"));
  const bool m1_makes_bolts = sROUTES.contains(machines.at("M1"), items.at("bolt"));
  // --8<-- [end:contains]
  DREAMOR_CHECK(m2_makes_bolts && !m1_makes_bolts);

  // --8<-- [start:membership]
  const auto as_item = [](cChild c) { return cItem{c}; };

  const auto sPARTS        = quantity.keys().select(as_item);     // the items another item takes
  const auto sPRODUCTS     = sMADE.where(!sPARTS.membership());   // the items no item takes
  const auto sNEXT_in_plan = sNEXT.where(sPERIODS.membership());  // sNEXT within the plan's periods
  // --8<-- [end:membership]
  static_assert(std::is_same_v<decltype(sPARTS), const dreamor::IndexSet<cItem>>);
  DREAMOR_CHECK(rowsOf(sPARTS) == "plate, bolt, nut");  // the bill of materials' children, each once
  DREAMOR_CHECK(rowsOf(sPRODUCTS) == "frame, bracket");
  DREAMOR_CHECK(sNEXT_in_plan.size() == 10);  // sNEXT's 15 rows but the 5 in period 4
  DREAMOR_CHECK(rowsOf(sNEXT_in_plan) ==
                "frame 2, frame 3, plate 2, plate 3, bracket 2, bracket 3, bolt 2, bolt 3, nut 2, nut 3");

  // --8<-- [start:membership-chain]
  const auto sROUTES_part = from<cMachine>(machines).lateral<cItem>(items_of).where(sPARTS.membership()).build();
  // --8<-- [end:membership-chain]
  static_assert(std::is_same_v<decltype(sROUTES_part), const dreamor::IndexSet<cMachine, cItem>>);
  DREAMOR_CHECK(rowsOf(sROUTES_part) == "M1 plate, M2 plate, M2 bolt, M2 nut");

  // Each derivation left its operand as it was.
  DREAMOR_CHECK(sMAKE.size() == 18 && sMADE.size() == 5 && sROUTES.size() == 6);
  DREAMOR_CHECK(rowsOf(sMADE) == "frame, plate, bracket, bolt, nut");
}

}  // namespace

int main() try {
  const Plant p = plant();
  derivingSets(p);
  return ::report("guide/deriving-sets");
} catch (...) { return ::uncaught(); }
