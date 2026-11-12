// SPDX-License-Identifier: MIT-0
//
// The guide's model, which every page's code stands on: a plant's items, the bill of materials between them, and
// the machines that make them, over three periods.
#pragma once

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"

namespace guide {

// --8<-- [start:entities]
struct Item {
  std::string name;
  double time;                 // hours a unit takes on a machine
  double hold;                 // the cost of holding a unit for a period
  std::vector<double> demand;  // the customers' demand in period t, at [t - 1]

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Machine {
  std::string name;
  double capacity;                        // hours in a period
  double setup;                           // the cost of setting up for an item in a period
  std::vector<dreamor::Ref<Item>> items;  // the items it makes

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

// --8<-- [end:entities]

// --8<-- [start:bom]
struct BomRecord {  // a line of the bill of materials: a unit of parent takes quantity units of child
  std::string parent, child;
  double quantity;
};

// --8<-- [end:bom]

// --8<-- [start:columns]
DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_ENTITY_COLUMN(cParent, Item);  // an item that takes another
DREAMOR_ENTITY_COLUMN(cChild, Item);   // an item another takes
DREAMOR_VALUE_COLUMN(cPeriod, int);
// --8<-- [end:columns]

struct Plant {
  dreamor::EntityVector<Item> items;
  dreamor::EntityVector<Machine> machines;
  std::vector<BomRecord> bom;
  int periods = 0;
};

inline Plant plant() {
  Plant p;
  // --8<-- [start:items-data]
  p.items.insert(Item{"frame", 1.0, 2.0, {1, 2, 2}});
  p.items.insert(Item{"plate", 0.5, 1.0, {0, 0, 0}});
  p.items.insert(Item{"bolt", 0.1, 0.1, {0, 0, 0}});
  p.items.insert(Item{"nut", 0.1, 0.1, {0, 5, 0}});      // spare nuts, sold on their own
  p.items.insert(Item{"bracket", 0.5, 1.0, {0, 0, 0}});  // a product no longer sold
  p.items.sort().freeze();
  // --8<-- [end:items-data]

  // --8<-- [start:machines-data]
  p.machines.insert(Machine{"M1", 4.0, 10.0, {p.items.at("frame"), p.items.at("plate"), p.items.at("bracket")}});
  p.machines.insert(Machine{"M2", 3.0, 5.0, {p.items.at("plate"), p.items.at("bolt"), p.items.at("nut")}});
  p.machines.sort().freeze();
  // --8<-- [end:machines-data]

  // --8<-- [start:bom-data]
  p.bom = {
      {"frame", "plate", 2},
      {"frame", "bolt", 4},
      {"plate", "bolt", 2},
      {"plate", "nut", 2},
      {"bracket", "plate", 1},
      {"bracket", "bolt", 2},
  };
  // --8<-- [end:bom-data]
  p.periods = 3;
  return p;
}

// --8<-- [start:bom-table]
using BomTable = dreamor::ParamTable<double, cParent, cChild>;  // a line's quantity, by its parent and child

inline BomTable bomTable(const Plant& p) {
  const auto edge_of = [&items = p.items](const BomRecord& r) {
    return std::tuple<cParent, cChild>{items.at(r.parent), items.at(r.child)};
  };
  const auto quantity_of = [](const BomRecord& r) { return r.quantity; };
  return BomTable{p.bom, edge_of, quantity_of};
}

// --8<-- [end:bom-table]

// The model's sets, written twice - as chains and in set-builder notation - to the same rows: what each machine makes
// of what is needed, in each period; what can be kept; what each parent made takes.
struct Sets {
  dreamor::IndexSet<cItem> needed;  // what is sold, and every part below it
  dreamor::IndexSet<cPeriod> periods;
  dreamor::IndexSet<cMachine, cItem, cPeriod> produce;            // what each machine can make, in each period
  dreamor::IndexSet<cItem, cPeriod> stock;                        // what can be kept
  dreamor::IndexSet<cParent, cChild, cMachine, cPeriod> consume;  // a parent made on a machine, and each child it takes
};

inline Sets byChain(const Plant& p, const BomTable& quantity) {
  using dreamor::index_set::relational::from;
  const auto items_of  = [](cMachine m) -> const auto& { return m->items; };
  const auto as_parent = [](cMachine m, cItem i, cPeriod t) { return std::tuple<cMachine, cParent, cPeriod>{m, i, t}; };
  const auto as_item   = [](cChild c) { return cItem{c}; };
  const auto is_sold   = [](cItem i) { return std::ranges::max(i->demand) > 0; };

  // --8<-- [start:needed-chain]
  const auto sSOLD   = from<cItem>(p.items).where(is_sold).build();
  const auto sBELOW  = dreamor::transitiveClosure(quantity.keys());  // every (parent, part) a path leads to
  const auto sNEEDED = dreamor::unite(sSOLD, from<cParent>(sSOLD).join(sBELOW).build().select(as_item));
  // --8<-- [end:needed-chain]

  // --8<-- [start:chain]
  const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, p.periods)).build();
  const auto sPRODUCE = from<cMachine>(p.machines).lateral<cItem>(items_of).join(sNEEDED).cross(sPERIODS).build();
  const auto sSTOCK   = from(sNEEDED).cross(sPERIODS).build();
  const auto sCONSUME = from(quantity.keys()).join(sPRODUCE.select(as_parent)).build();
  // --8<-- [end:chain]
  return Sets{sNEEDED, sPERIODS, sPRODUCE, sSTOCK, sCONSUME};
}

inline Sets bySetBuilder(const Plant& p, const BomTable& quantity) {
  using namespace dreamor::index_set::set_builder;
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };
  const auto periods  = dreamor::interval::closed(1, p.periods);
  const auto as_item  = [](cChild c) { return cItem{c}; };
  const auto is_sold  = [](cItem i) { return std::ranges::max(i->demand) > 0; };

  // --8<-- [start:needed-set-builder]
  const auto sSOLD   = of<cItem>(in<cItem>(p.items)).where(is_sold).build();
  const auto sBELOW  = dreamor::transitiveClosure(quantity.keys());
  const auto sPARTS  = of<cParent, cChild>(in<cParent, cChild>(sBELOW), in<cParent>(sSOLD)).build().select(as_item);
  const auto sNEEDED = dreamor::unite(sSOLD, sPARTS);
  // --8<-- [end:needed-set-builder]

  // --8<-- [start:set-builder]
  const auto sPERIODS = of<cPeriod>(in<cPeriod>(periods)).build();
  const auto sPRODUCE = of<cMachine, cItem, cPeriod>(
                            in<cMachine>(p.machines), in<cItem>(items_of), in<cItem>(sNEEDED), in<cPeriod>(periods))
                            .build();
  const auto sSTOCK   = of<cItem, cPeriod>(in<cItem>(sNEEDED), in<cPeriod>(periods)).build();
  const auto sCONSUME =
      of<cParent, cChild, cMachine, cPeriod>(in<cParent, cChild>(quantity.keys()),
                                             in<cMachine>(p.machines),
                                             in<cParent>(items_of),  // a condition: m makes the parent
                                             in<cParent>(sNEEDED),   // and the parent is needed
                                             in<cPeriod>(periods))
          .build();
  // --8<-- [end:set-builder]
  return Sets{sNEEDED, sPERIODS, sPRODUCE, sSTOCK, sCONSUME};
}

}  // namespace guide
