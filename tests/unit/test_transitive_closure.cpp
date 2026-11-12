// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// transitiveClosure: a diamond gives each pair once, in the closure's order; a cycle and a self-loop are welcome and
// show as a node's row with itself; an empty relation; a bill of materials over entities, its every part below a
// product read off groupBy, and R* as R+ united with each node's pair with itself.
#include <algorithm>
#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

DREAMOR_VALUE_COLUMN(cFrom, int);
DREAMOR_VALUE_COLUMN(cTo, int);

struct Part {
  std::string code;

  [[nodiscard]] const std::string& primaryKey() const { return code; }
};

DREAMOR_ENTITY_COLUMN(cPart, Part);
DREAMOR_ENTITY_COLUMN(cParent, Part);
DREAMOR_ENTITY_COLUMN(cChild, Part);

using Edge = std::tuple<cFrom, cTo>;

dreamor::IndexSet<cFrom, cTo> relation(const std::vector<Edge>& edges) {
  return dreamor::index_set::relational::from(edges).build();
}

void diamond() {
  // a -> b, a -> c, b -> d, c -> d: d is below a twice over, and one row
  const auto sBELOW = dreamor::transitiveClosure(relation({{1, 2}, {1, 3}, {2, 4}, {3, 4}}));
  static_assert(std::is_same_v<decltype(sBELOW), const dreamor::IndexSet<cFrom, cTo>>);
  const std::vector<Edge> expected{{1, 2}, {1, 3}, {1, 4}, {2, 4}, {3, 4}};  // sources as listed, nearest first
  DREAMOR_CHECK(std::ranges::equal(sBELOW, expected));
}

void cycles() {
  // 1 -> 2 -> 3 -> 1: every node reaches every node, itself too
  const auto sROUND = dreamor::transitiveClosure(relation({{1, 2}, {2, 3}, {3, 1}}));
  DREAMOR_CHECK(sROUND.size() == 9 && sROUND.contains(cFrom{1}, cTo{1}) && sROUND.contains(cFrom{3}, cTo{2}));
  // a self-loop is a cycle of one; a node no edge leaves is no source
  const auto sLOOP = dreamor::transitiveClosure(relation({{5, 5}, {5, 6}}));
  const std::vector<Edge> loop{{5, 5}, {5, 6}};
  DREAMOR_CHECK(std::ranges::equal(sLOOP, loop));
  // the rows with themselves are the nodes on a cycle
  const auto on_itself = [](cFrom a, cTo b) { return a.get() == b.get(); };
  DREAMOR_CHECK(sROUND.where(on_itself).size() == 3 && sLOOP.where(on_itself).size() == 1);
  DREAMOR_CHECK(dreamor::transitiveClosure(relation({{1, 2}, {2, 3}})).where(on_itself).empty());
  // nothing in, nothing out
  DREAMOR_CHECK(dreamor::transitiveClosure(relation({})).empty());
}

void billOfMaterials() {
  dreamor::EntityVector<Part> parts;
  for (const char* code : {"bike", "frame", "wheel", "spoke", "tube"}) {
    static_cast<void>(parts.emplace(Part{code}));
  }
  parts.freeze();
  const auto p = [&parts](const char* code) { return parts.at(code); };
  // bike: frame and two wheels' worth; frame and wheel both use tube; wheel uses spoke
  const std::vector<std::tuple<cParent, cChild>> bom{
      {p("bike"), p("frame")},
      {p("bike"), p("wheel")},
      {p("frame"), p("tube")},
      {p("wheel"), p("spoke")},
      {p("wheel"), p("tube")},
  };
  const auto sBOM   = dreamor::index_set::relational::from(bom).build();
  const auto sBELOW = dreamor::transitiveClosure(sBOM);
  DREAMOR_CHECK(sBELOW.size() == 7 && sBELOW.contains(cParent{p("bike")}, cChild{p("tube")}) &&
                sBELOW.contains(cParent{p("bike")}, cChild{p("spoke")}) &&
                !sBELOW.contains(cParent{p("frame")}, cChild{p("spoke")}));
  // every part below the bike, read off a group
  const auto below = sBELOW.groupBy<cParent>();
  DREAMOR_CHECK(below.slice(p("bike")).size() == 4 && below.slice(p("wheel")).size() == 2);
  // R*: the part itself and all below it - R+ united with each part's pair with itself
  const auto sPARTS = dreamor::index_set::relational::from<cPart>(parts).build();
  const auto itself = [](cPart part) { return std::tuple<cParent, cChild>{cParent{part}, cChild{part}}; };
  const auto sFROM  = dreamor::unite(sBELOW, sPARTS.select(itself));
  DREAMOR_CHECK(sFROM.size() == 12 && sFROM.contains(cParent{p("tube")}, cChild{p("tube")}) &&
                sFROM.contains(cParent{p("bike")}, cChild{p("bike")}));
}

}  // namespace

int main() try {
  diamond();
  cycles();
  billOfMaterials();
  return report("transitive_closure");
} catch (...) { return ::uncaught(); }
