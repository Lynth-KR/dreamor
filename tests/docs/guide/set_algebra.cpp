// SPDX-License-Identifier: MIT-0
//
// docs/guide/set-algebra.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <cstddef>
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

// The items' names, in the set's order.
std::vector<std::string> names(const dreamor::IndexSet<cItem>& set) {
  std::vector<std::string> out;
  for (const auto& [item] : set) {
    out.push_back(item->name);
  }
  return out;
}

using Names = std::vector<std::string>;

void setAlgebra(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;
  const BomTable quantity                  = bomTable(p);
  const cParent frame{items.at("frame")};
  const cParent bracket{items.at("bracket")};
  const cChild bolt{items.at("bolt")};
  const cChild nut{items.at("nut")};

  // --8<-- [start:pair]
  using dreamor::index_set::relational::from;

  const auto is_sold = [](cItem i) { return std::ranges::max(i->demand) > 0; };
  const auto as_item = [](cChild c) { return cItem{c}; };

  const auto sSOLD  = from<cItem>(items).where(is_sold).build();  // what customers buy
  const auto sPARTS = quantity.keys().select(as_item);            // what another item takes
  // --8<-- [end:pair]
  DREAMOR_CHECK(names(sSOLD) == Names{"frame", "nut"});  // the items with demand, in the items' order
  // The bill of materials' children, each once in the order the lines first name them: plate, bolt, nut.
  DREAMOR_CHECK(names(sPARTS) == Names{"plate", "bolt", "nut"});

  // --8<-- [start:combine]
  const auto sSOLD_OR_PART = dreamor::unite(sSOLD, sPARTS);       // frame, nut, plate, bolt
  const auto sSOLD_PART    = dreamor::intersect(sSOLD, sPARTS);   // nut
  const auto sEND_PRODUCTS = dreamor::difference(sSOLD, sPARTS);  // frame: sold, and a part of no item
  // --8<-- [end:combine]
  DREAMOR_CHECK(names(sSOLD_OR_PART) == Names{"frame", "nut", "plate", "bolt"});  // sSOLD's, then what it lacks
  DREAMOR_CHECK(names(sSOLD_PART) == Names{"nut"});
  DREAMOR_CHECK(names(sEND_PRODUCTS) == Names{"frame"});
  DREAMOR_CHECK(!sSOLD_OR_PART.contains(items.at("bracket")));  // neither sold nor a part

  // --8<-- [start:compare]
  const auto sPART_OR_SOLD = dreamor::unite(sPARTS, sSOLD);  // plate, bolt, nut, frame

  const bool same_rows  = sPART_OR_SOLD == sSOLD_OR_PART;                    // true
  const bool same_order = std::ranges::equal(sPART_OR_SOLD, sSOLD_OR_PART);  // false
  const bool all_parts  = dreamor::within(sSOLD, sPARTS);                    // false: the frame is no part
  const bool part_too   = dreamor::within(sSOLD_PART, sPARTS);               // true
  // --8<-- [end:compare]
  DREAMOR_CHECK(names(sPART_OR_SOLD) == Names{"plate", "bolt", "nut", "frame"});
  DREAMOR_CHECK(same_rows && !same_order);
  DREAMOR_CHECK(!all_parts && part_too);

  // --8<-- [start:closure]
  const auto sBELOW    = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
  const bool lines_too = dreamor::within(quantity.keys(), sBELOW);     // true: a line is a path of one edge
  // --8<-- [end:closure]
  static_assert(std::is_same_v<decltype(sBELOW), const dreamor::IndexSet<cParent, cChild>>);
  // The six lines, and the nut below the frame and below the bracket, each through the plate.
  DREAMOR_CHECK(sBELOW.size() == 8 && lines_too);
  DREAMOR_CHECK(sBELOW.contains(frame, nut) && sBELOW.contains(bracket, nut));
  std::size_t frame_bolt = 0;  // frame takes bolts directly and through the plate
  for (const auto& [parent, child] : sBELOW) {
    if (parent == frame && child == bolt) { ++frame_bolt; }
  }
  DREAMOR_CHECK(frame_bolt == 1);

  // --8<-- [start:by-name]
  const auto sUSED_IN   = quantity.keys().select<cChild, cParent>();  // each line, its part first
  const bool same_lines = sUSED_IN == quantity.keys();                // true
  const auto sINDIRECT  = dreamor::difference(sBELOW, sUSED_IN);      // the pairs no line joins
  // --8<-- [end:by-name]
  static_assert(std::is_same_v<decltype(sUSED_IN), const dreamor::IndexSet<cChild, cParent>>);
  static_assert(std::is_same_v<decltype(sINDIRECT), const dreamor::IndexSet<cParent, cChild>>);  // the left's
  DREAMOR_CHECK(same_lines);
  DREAMOR_CHECK(sINDIRECT.size() == 2 && sINDIRECT.contains(frame, nut) && sINDIRECT.contains(bracket, nut));

  // The model's own needed set (model.hpp, needed-chain): the sold frame and nut, and the plate, bolt and nut below
  // the frame.
  const Sets sets = byChain(p, quantity);
  DREAMOR_CHECK(names(sets.NEEDED) == Names{"frame", "nut", "plate", "bolt"});

  // --8<-- [start:reflexive]
  const auto as_self = [](cItem i) { return std::tuple<cParent, cChild>{i, i}; };  // an item and itself

  const auto sSELF        = from<cItem>(items).build().select(as_self);
  const auto sAT_OR_BELOW = dreamor::unite(sBELOW, sSELF);  // an item, and every item below it
  // --8<-- [end:reflexive]
  DREAMOR_CHECK(sSELF.size() == 5);  // one pair for each of the five items
  // The bill of materials has no cycle, so the closure pairs no item with itself.
  for (const auto& [item, same_item] : sSELF) {
    DREAMOR_CHECK(!sBELOW.contains(item, same_item));
  }
  DREAMOR_CHECK(sAT_OR_BELOW.size() == 13);  // the closure's 8 pairs and the 5, none of them in the closure
  DREAMOR_CHECK(sAT_OR_BELOW.contains(frame, cChild{items.at("frame")}) && sAT_OR_BELOW.contains(frame, nut) &&
                sAT_OR_BELOW.contains(cParent{items.at("nut")}, nut));
  DREAMOR_CHECK(!sAT_OR_BELOW.contains(cParent{items.at("nut")}, cChild{items.at("frame")}));
  std::size_t at_or_below_frame = 0;  // the frame itself, and the plate, the bolt and the nut below it
  std::size_t at_or_below_nut   = 0;  // the nut alone: it takes nothing
  for (const auto& [parent, child] : sAT_OR_BELOW) {
    if (parent == frame) { ++at_or_below_frame; }
    if (parent == items.at("nut")) { ++at_or_below_nut; }
  }
  DREAMOR_CHECK(at_or_below_frame == 4 && at_or_below_nut == 1);
  DREAMOR_CHECK(sAT_OR_BELOW.contains(frame, cChild{items.at("plate")}) && sAT_OR_BELOW.contains(frame, bolt));
}

}  // namespace

int main() try {
  const Plant p = plant();
  setAlgebra(p);
  return ::report("guide/set-algebra");
} catch (...) { return ::uncaught(); }
