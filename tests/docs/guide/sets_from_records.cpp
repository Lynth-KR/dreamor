// SPDX-License-Identifier: MIT-0
//
// docs/guide/sets-from-records.md: the page shows the regions between --8<-- lines, and the checks stay here.
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

// --8<-- [start:member]
struct Bom {
  explicit Bom(const Plant& p) : sBOM{edges(p)} {}

  dreamor::IndexSet<cParent, cChild> sBOM;

  static dreamor::IndexSet<cParent, cChild> edges(const Plant& p) {
    const auto edge_of = [&items = p.items](const BomRecord& r) {
      return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
    };
    return dreamor::IndexSet<cParent, cChild>{p.bom, edge_of};
  }
};

// --8<-- [end:member]

static_assert(!std::is_default_constructible_v<dreamor::IndexSet<cParent, cChild>>);  // no empty set to fill later

// Whether the set has a row for each record, in the records' order, its parent and child named as the record names
// them: read off the set's rows, against the records' text.
bool rowsFollow(const dreamor::IndexSet<cParent, cChild>& set, const std::vector<BomRecord>& records) {
  if (set.size() != records.size()) { return false; }
  std::size_t k = 0;
  for (const auto& [parent, child] : set) {
    if (parent->name != records[k].parent || child->name != records[k].child) { return false; }
    ++k;
  }
  return true;
}

void fromRecords(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;
  const std::vector<BomRecord>& bom        = p.bom;

  // --8<-- [start:bom-set]
  const auto edge_of = [&items](const BomRecord& r) {
    return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
  };
  const dreamor::IndexSet<cParent, cChild> sBOM{bom, edge_of};
  // --8<-- [end:bom-set]
  DREAMOR_CHECK(sBOM.size() == 6);  // bom-data's six lines, a row each
  DREAMOR_CHECK(rowsFollow(sBOM, bom));
  // The first record is the frame's plate, and the last the bracket's bolt.
  DREAMOR_CHECK(*sBOM.begin() == std::tuple<cParent, cChild>{items.at("frame"), items.at("plate")});
  DREAMOR_CHECK(*(sBOM.end() - 1) == std::tuple<cParent, cChild>{items.at("bracket"), items.at("bolt")});
  DREAMOR_CHECK(sBOM.contains(cParent{items.at("bracket")}, cChild{items.at("bolt")}));
  DREAMOR_CHECK(!sBOM.contains(cParent{items.at("bolt")}, cChild{items.at("bracket")}));  // parent, then child
}

void member(const Plant& p) {
  const Bom bom{p};
  DREAMOR_CHECK(rowsFollow(bom.sBOM, p.bom));
  const auto& [parent, child] = *bom.sBOM.begin();
  DREAMOR_CHECK(&*parent == &*p.items.at("frame") && &*child == &*p.items.at("plate"));  // the plant's items
}

void oneColumn(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;

  // --8<-- [start:one-column]
  const std::vector<std::string> bought{"bolt", "nut"};  // items a supplier sells too
  const auto item_named = [&items](const std::string& name) { return items.at(name); };
  const dreamor::IndexSet<cItem> sBOUGHT{bought, item_named};
  // --8<-- [end:one-column]
  DREAMOR_CHECK(sBOUGHT.size() == 2 && sBOUGHT.contains(items.at("bolt")) && sBOUGHT.contains(items.at("nut")));
  DREAMOR_CHECK(std::get<cItem>(*sBOUGHT.begin())->name == "bolt");  // in the records' order
}

}  // namespace

int main() try {
  const Plant p = plant();
  fromRecords(p);
  member(p);
  oneColumn(p);
  return ::report("guide/sets-from-records");
} catch (...) { return ::uncaught(); }
