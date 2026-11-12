// SPDX-License-Identifier: MIT-0
//
// docs/guide/relational-algebra.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <string>
#include <type_traits>

#include "docs/guide/model.hpp"
#include "docs/log_capture.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

void relationalAlgebra(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const int periods                              = p.periods;  // 3
  const BomTable quantity                        = bomTable(p);
  const auto m1                                  = machines.at("M1");
  const auto m2                                  = machines.at("M2");

  // --8<-- [start:chain]
  using dreamor::index_set::relational::from;

  const auto items_of   = [](cMachine m) -> const auto& { return m->items; };
  const auto fits_batch = [](cMachine m, cItem i) { return 8 * i->time <= m->capacity; };  // a batch of 8 units

  const auto sBATCH = from<cMachine>(machines)
                          .lateral<cItem>(items_of)
                          .cross<cPeriod>(dreamor::interval::closed(1, periods))
                          .where(fits_batch)
                          .build();
  // --8<-- [end:chain]
  static_assert(std::is_same_v<decltype(sBATCH), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  // M1 (4 hours) fits 8 plates or brackets (4 hours) but not 8 frames (8 hours); M2 (3 hours) fits 8 bolts or nuts
  // (0.8 hours) but not 8 plates: 4 pairs, in 3 periods each.
  DREAMOR_CHECK(sBATCH.size() == 12);
  DREAMOR_CHECK(sBATCH.contains(m1, items.at("plate"), 1) && sBATCH.contains(m1, items.at("bracket"), 2) &&
                sBATCH.contains(m2, items.at("bolt"), 1) && sBATCH.contains(m2, items.at("nut"), 3));
  DREAMOR_CHECK(!sBATCH.contains(m1, items.at("frame"), 1) && !sBATCH.contains(m2, items.at("plate"), 1));
  DREAMOR_CHECK(!sBATCH.contains(m1, items.at("nut"), 1));  // nuts fit M1 but M1 does not make them

  // --8<-- [start:braces]
  const auto sSERVICE = from<cMachine>(machines).cross<cPeriod>({1, 3}).build();  // each machine's service periods
  // --8<-- [end:braces]
  static_assert(std::is_same_v<decltype(sSERVICE), const dreamor::IndexSet<cMachine, cPeriod>>);
  DREAMOR_CHECK(sSERVICE.size() == 4);  // 2 machines, 2 periods each
  DREAMOR_CHECK(sSERVICE.contains(m1, 1) && sSERVICE.contains(m1, 3) && sSERVICE.contains(m2, 1) &&
                sSERVICE.contains(m2, 3));
  DREAMOR_CHECK(!sSERVICE.contains(m1, 2) && !sSERVICE.contains(m2, 2));

  // --8<-- [start:cross]
  const auto sPERIODS  = from<cPeriod>(dreamor::interval::closed(1, periods)).build();
  const auto sCAPACITY = from<cMachine>(machines).cross(sPERIODS).build();  // a machine's hours in a period
  // --8<-- [end:cross]
  static_assert(std::is_same_v<decltype(sCAPACITY), const dreamor::IndexSet<cMachine, cPeriod>>);
  // closed(1, 3) holds the periods from 1 to 3, both ends included.
  DREAMOR_CHECK(sPERIODS.size() == 3 && sPERIODS.contains(1) && sPERIODS.contains(3));
  DREAMOR_CHECK(sCAPACITY.size() == 6);  // 2 machines times 3 periods
  DREAMOR_CHECK(sCAPACITY.contains(m1, 1) && sCAPACITY.contains(m2, 3));

  // --8<-- [start:needed]
  const auto is_sold = [](cItem i) { return std::ranges::max(i->demand) > 0; };
  const auto as_item = [](cChild c) { return cItem{c}; };

  const auto sSOLD       = from<cItem>(items).where(is_sold).build();
  const auto sBELOW      = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
  const auto sSOLD_BELOW = from<cParent>(sSOLD).join(sBELOW).build();    // a sold item and each part below it
  const auto sNEEDED     = dreamor::unite(sSOLD, sSOLD_BELOW.select(as_item));
  // --8<-- [end:needed]
  static_assert(std::is_same_v<decltype(sSOLD), const dreamor::IndexSet<cItem>>);
  DREAMOR_CHECK(sSOLD.size() == 2 && sSOLD.contains(items.at("frame")) && sSOLD.contains(items.at("nut")));
  static_assert(std::is_same_v<decltype(sSOLD_BELOW), const dreamor::IndexSet<cParent, cChild>>);
  // frame takes plates and bolts, and a plate takes bolts and nuts: frame's parts are plate, bolt and nut; a nut
  // takes nothing.
  DREAMOR_CHECK(sSOLD_BELOW.size() == 3);
  const cParent frame{items.at("frame")};
  DREAMOR_CHECK(sSOLD_BELOW.contains(frame, cChild{items.at("plate")}) &&
                sSOLD_BELOW.contains(frame, cChild{items.at("bolt")}) &&
                sSOLD_BELOW.contains(frame, cChild{items.at("nut")}));
  // No line of the bill of materials has the frame take a nut; the frame takes plates, and a plate takes nuts.
  DREAMOR_CHECK(!quantity.contains(frame, cChild{items.at("nut")}) && sBELOW.contains(frame, cChild{items.at("nut")}));
  DREAMOR_CHECK(sNEEDED.size() == 4 && sNEEDED.contains(items.at("frame")) && sNEEDED.contains(items.at("nut")) &&
                sNEEDED.contains(items.at("plate")) && sNEEDED.contains(items.at("bolt")));
  DREAMOR_CHECK(!sNEEDED.contains(items.at("bracket")));

  // --8<-- [start:join]
  const auto sPRODUCE = from<cMachine>(machines).lateral<cItem>(items_of).join(sNEEDED).cross(sPERIODS).build();
  // --8<-- [end:join]
  static_assert(std::is_same_v<decltype(sPRODUCE), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  DREAMOR_CHECK(sPRODUCE.size() == 15);  // M1's frame and plate, M2's plate, bolt and nut, in 3 periods each
  DREAMOR_CHECK(sPRODUCE.contains(m1, items.at("frame"), 1) && sPRODUCE.contains(m2, items.at("nut"), 3));
  DREAMOR_CHECK(!sPRODUCE.contains(m1, items.at("bracket"), 1));

  // --8<-- [start:reuse]
  const auto made       = from<cMachine>(machines).lateral<cItem>(items_of);
  const auto sMADE_SOLD = made.join(sSOLD).build();  // the items customers buy, on the machine that makes them
  const auto sMADE      = made.build();              // every item each machine makes
  // --8<-- [end:reuse]
  DREAMOR_CHECK(sMADE_SOLD.size() == 2 && sMADE_SOLD.contains(m1, items.at("frame")) &&
                sMADE_SOLD.contains(m2, items.at("nut")));
  DREAMOR_CHECK(sMADE.size() == 6);  // 3 items on each machine

  // --8<-- [start:staged]
  const auto has_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };

  const auto sSALES = from<cItem>(items).cross(sPERIODS).where(has_demand).build();  // the few rows customers buy
  const auto sSERVE = from<cMachine>(machines).lateral<cItem>(items_of).join(sSALES).build();
  // --8<-- [end:staged]
  static_assert(std::is_same_v<decltype(sSALES), const dreamor::IndexSet<cItem, cPeriod>>);
  DREAMOR_CHECK(sSALES.size() == 4);  // frame in periods 1, 2 and 3, nut in 2
  static_assert(std::is_same_v<decltype(sSERVE), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  const auto serve_one_chain = from<cMachine>(machines).lateral<cItem>(items_of).cross(sPERIODS).where(has_demand);
  // The visit order is a compile-time constant. In one chain, the item is bound last, from its machine's list, so
  // has_demand runs on each machine's item in each period: 6 items of the machines, in 3 periods.
  static_assert(decltype(serve_one_chain)::visitOrder() == "{cPeriod}, cMachine, cItem");
  const auto sSERVE_ONE_CHAIN = serve_one_chain.build();
  DREAMOR_CHECK(sSERVE.size() == 4 && sSERVE == sSERVE_ONE_CHAIN);  // M1's frame in 1, 2 and 3, M2's nut in 2
  DREAMOR_CHECK(sSERVE.contains(m1, items.at("frame"), 1) && sSERVE.contains(m1, items.at("frame"), 3) &&
                sSERVE.contains(m2, items.at("nut"), 2));

  const LogCapture log;
  DREAMOR_CHECK(log.isOpen());

  // --8<-- [start:visit-order]
  const auto serve = from<cMachine>(machines).lateral<cItem>(items_of).join(sSALES);
  DREAMOR_LOG_INFO("sSERVE visits {}", serve.visitOrder());  // cMachine, cItem, {cItem, cPeriod}
  // --8<-- [end:visit-order]
  const std::string text = log.text();

  DREAMOR_CHECK(text.ends_with("[INFO] sSERVE visits cMachine, cItem, {cItem, cPeriod}\n"));
  DREAMOR_CHECK(serve.visitOrder() == "cMachine, cItem, {cItem, cPeriod}");
  static_assert(decltype(serve)::visitOrder() == "cMachine, cItem, {cItem, cPeriod}");
  DREAMOR_CHECK(serve.build() == sSERVE);
}

}  // namespace

int main() try {
  const Plant p = plant();
  relationalAlgebra(p);
  return ::report("guide/relational-algebra");
} catch (...) { return ::uncaught(); }
