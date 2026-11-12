// SPDX-License-Identifier: MIT-0
//
// docs/guide/set-builder-notation.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <tuple>
#include <type_traits>

#include "docs/guide/model.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

void write(const Plant& p) {
  // --8<-- [start:write]
  using namespace dreamor::index_set::set_builder;

  const auto items_of  = [](cMachine m) -> const auto& { return m->items; };
  const auto periods   = dreamor::interval::closed(1, p.periods);
  const auto in_demand = [](cItem i, cPeriod t) { return i->demand[t - 1] > 0; };

  const auto sSERVE = of<cMachine, cItem, cPeriod>(in<cMachine>(p.machines), in<cItem>(items_of), in<cPeriod>(periods))
                          .where(in_demand)
                          .build();
  // --8<-- [end:write]
  static_assert(std::is_same_v<decltype(sSERVE), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  // Demand is the frame's in periods 1 to 3, made on M1, and the nut's in period 2, made on M2: 4 rows.
  const cMachine m1 = p.machines.at("M1");
  const cMachine m2 = p.machines.at("M2");
  const cItem frame = p.items.at("frame");
  const cItem nut   = p.items.at("nut");
  DREAMOR_CHECK(sSERVE.size() == 4);
  DREAMOR_CHECK(sSERVE.contains(m1, frame, 1) && sSERVE.contains(m1, frame, 2) && sSERVE.contains(m1, frame, 3));
  DREAMOR_CHECK(sSERVE.contains(m2, nut, 2));

  // The chain that writes the same set.
  using dreamor::index_set::relational::from;
  const auto sSERVE_CHAIN =
      from<cMachine>(p.machines).lateral<cItem>(items_of).cross<cPeriod>(periods).where(in_demand).build();
  DREAMOR_CHECK(sSERVE == sSERVE_CHAIN);

  // --8<-- [start:any-order]
  const auto sSERVE_TOO =
      of<cMachine, cItem, cPeriod>(in<cPeriod>(periods), in<cMachine>(p.machines), in<cItem>(items_of))
          .where(in_demand)
          .build();
  // --8<-- [end:any-order]
  static_assert(std::is_same_v<decltype(sSERVE_TOO), const dreamor::IndexSet<cMachine, cItem, cPeriod>>);
  DREAMOR_CHECK(sSERVE_TOO == sSERVE);
  // build() reads the sources in one order for both, whatever the order written, so the rows come in one order too.
  using Serve =
      decltype(of<cMachine, cItem, cPeriod>(in<cMachine>(p.machines), in<cItem>(items_of), in<cPeriod>(periods))
                   .where(in_demand));
  using ServeToo =
      decltype(of<cMachine, cItem, cPeriod>(in<cPeriod>(periods), in<cMachine>(p.machines), in<cItem>(items_of))
                   .where(in_demand));
  static_assert(Serve::visitOrder() == "cPeriod, cMachine, cItem" &&
                ServeToo::visitOrder() == "cPeriod, cMachine, cItem");
  DREAMOR_CHECK(std::ranges::equal(sSERVE_TOO, sSERVE));
}

void bindTogether(const Plant& p) {
  using namespace dreamor::index_set::set_builder;
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };
  const auto periods  = dreamor::interval::closed(1, p.periods);

  // --8<-- [start:bind-together]
  const auto sROUTES        = of<cMachine, cItem>(in<cMachine>(p.machines), in<cItem>(items_of)).build();
  const auto sROUTE_PERIODS = of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(sROUTES), in<cPeriod>(periods)).build();
  // --8<-- [end:bind-together]
  DREAMOR_CHECK(sROUTES.size() == 6);          // M1 and M2 make 3 items each
  DREAMOR_CHECK(sROUTE_PERIODS.size() == 18);  // each of the 6 routes in each of the 3 periods
  for (const auto& [m, i] : sROUTES) {
    DREAMOR_CHECK(sROUTE_PERIODS.contains(m, i, 1) && sROUTE_PERIODS.contains(m, i, 2) &&
                  sROUTE_PERIODS.contains(m, i, 3));
  }
}

void condition(const Plant& p) {
  const auto edge_of = [&items = p.items](const BomRecord& r) {
    return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
  };
  const dreamor::IndexSet<cParent, cChild> sBOM{p.bom, edge_of};
  using namespace dreamor::index_set::set_builder;
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };

  // --8<-- [start:condition]
  const auto sCONSUME =
      of<cParent, cChild, cMachine>(in<cParent, cChild>(sBOM), in<cMachine>(p.machines), in<cParent>(items_of)).build();
  // --8<-- [end:condition]
  static_assert(std::is_same_v<decltype(sCONSUME), const dreamor::IndexSet<cParent, cChild, cMachine>>);
  // Each line of the bill once for each machine that makes its parent: the frame's 2 and the bracket's 2 on M1, the
  // plate's 2 on M1 and on M2.
  const cMachine m1 = p.machines.at("M1");
  const cMachine m2 = p.machines.at("M2");
  const cParent frame{p.items.at("frame")};
  const cParent plate{p.items.at("plate")};
  const cChild plateAsChild{p.items.at("plate")};
  const cChild bolt{p.items.at("bolt")};
  const cChild nut{p.items.at("nut")};
  DREAMOR_CHECK(sCONSUME.size() == 8);
  DREAMOR_CHECK(sCONSUME.contains(plate, nut, m1) && sCONSUME.contains(plate, nut, m2));
  DREAMOR_CHECK(sCONSUME.contains(frame, plateAsChild, m1) && sCONSUME.contains(frame, bolt, m1));
  // M2 makes no frame, so neither of the frame's lines comes with M2.
  DREAMOR_CHECK(!sCONSUME.contains(frame, plateAsChild, m2) && !sCONSUME.contains(frame, bolt, m2));
  // M1 alone makes the bracket.
  const cParent bracket{p.items.at("bracket")};
  DREAMOR_CHECK(sCONSUME.contains(bracket, plateAsChild, m1) && sCONSUME.contains(bracket, bolt, m1));
  DREAMOR_CHECK(!sCONSUME.contains(bracket, plateAsChild, m2) && !sCONSUME.contains(bracket, bolt, m2));
}

void modelSets(const Plant& p) {
  const BomTable quantity = bomTable(p);
  const Sets chains       = byChain(p, quantity);
  const Sets written      = bySetBuilder(p, quantity);
  // Each set equals its namesake in the other notation.
  DREAMOR_CHECK(chains.needed == written.needed);  // the frame, the nut, the plate and the bolt
  DREAMOR_CHECK(chains.periods == written.periods && chains.produce == written.produce);
  DREAMOR_CHECK(chains.stock == written.stock && chains.consume == written.consume);

  const dreamor::IndexSet<cMachine, cItem, cPeriod>& sPRODUCE           = written.produce;
  const dreamor::IndexSet<cParent, cChild, cMachine, cPeriod>& sCONSUME = written.consume;
  const cMachine m1                                                     = p.machines.at("M1");
  const cMachine m2                                                     = p.machines.at("M2");
  // The bracket is not needed: M1's frame and plate, M2's plate, bolt and nut, in 3 periods each.
  DREAMOR_CHECK(sPRODUCE.size() == 15 && !sPRODUCE.contains(m1, p.items.at("bracket"), 1));
  DREAMOR_CHECK(sPRODUCE.contains(m1, p.items.at("frame"), 1) && sPRODUCE.contains(m2, p.items.at("nut"), 3));
  // The frame's and the plate's lines, each with the machines that make its parent, in each period; no bracket.
  const cParent frame{p.items.at("frame")};
  const cParent plate{p.items.at("plate")};
  const cParent bracket{p.items.at("bracket")};
  const cChild bolt{p.items.at("bolt")};
  DREAMOR_CHECK(sCONSUME.contains(frame, bolt, m1, 1) && !sCONSUME.contains(frame, bolt, m2, 1));
  DREAMOR_CHECK(sCONSUME.contains(plate, bolt, m1, 2) && sCONSUME.contains(plate, bolt, m2, 2));
  DREAMOR_CHECK(!sCONSUME.contains(bracket, bolt, m1, 1));
}

}  // namespace

int main() try {
  const Plant p = plant();
  write(p);
  bindTogether(p);
  condition(p);
  modelSets(p);
  return ::report("guide/set-builder-notation");
} catch (...) { return ::uncaught(); }
