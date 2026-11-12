// SPDX-License-Identifier: MIT-0
//
// docs/guide/entities.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "docs/guide/model.hpp"
#include "docs/log_capture.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/logger.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;

// --8<-- [start:fixture]
struct Fixture {  // a fixture is named on its machine: M1's F1 and M2's F1 are two fixtures
  dreamor::Ref<Machine> machine;
  std::string name;

  [[nodiscard]] auto primaryKey() const { return std::tuple{machine, name}; }
};

// --8<-- [end:fixture]

static_assert(dreamor::entity<Item> && dreamor::entity<Machine> && dreamor::entity<Fixture>);

void holdOnce() {
  // --8<-- [start:hold-once]
  dreamor::EntityVector<Item> items;
  items.insert(Item{"washer", 0.1, 0.1, {0, 3, 0}});
  items.insert(Item{"gear", 0.2, 0.1, {0, 1, 0}});
  const bool added = items.insert(Item{"gear", 0.3, 0.2, {0, 0, 0}});  // false: a gear is held already
  items.sort().freeze();                                               // gear, washer
  // --8<-- [end:hold-once]
  DREAMOR_CHECK(!added && items.size() == 2);                // the washer and the first gear
  DREAMOR_CHECK(exactlyEqual(items.at("gear")->time, 0.2));  // the gear held first stays as it was
  DREAMOR_CHECK(items.frozen() && items[0].name == "gear" && items[1].name == "washer");
}

void lookUp(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;
  // The model's items, in key order: bolt, bracket, frame, nut, plate.
  DREAMOR_CHECK(items.size() == 5 && items[0].name == "bolt" && items[1].name == "bracket" &&
                items[2].name == "frame" && items[3].name == "nut" && items[4].name == "plate");

  const LogCapture log;
  DREAMOR_CHECK(log.isOpen());

  // --8<-- [start:look-up]
  const dreamor::Ref<Item> frame = items.at("frame");  // std::out_of_range if no item has the key

  const std::string_view wanted = "gear";
  const dreamor::Ref<Item> gear = items.find(wanted);  // empty: no item is named gear
  if (!gear) { DREAMOR_LOG_WARNING("no item named {}", wanted); }
  // --8<-- [end:look-up]
  const std::string text = log.text();

  DREAMOR_CHECK(frame && frame->name == "frame");
  DREAMOR_CHECK(&*frame == &items[2]);  // the frame that items holds, third in key order, not a copy
  DREAMOR_CHECK(!gear && items.find("frame") == frame);
  DREAMOR_CHECK(text.ends_with("[WARN] no item named gear\n") && text.find("[WARN]") == text.rfind("[WARN]"));
  DREAMOR_CHECK_THROWS(std::out_of_range, items.at("gear"));
}

void atHand(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;

  // --8<-- [start:at-hand]
  std::vector<dreamor::Ref<Item>> slow;  // the items that take half an hour or more a unit
  for (const Item& item : items) {
    if (item.time >= 0.5) { slow.push_back(items.ref(item)); }  // the item in hand, with no lookup of its key
  }

  std::vector<dreamor::Ref<Item>> every;  // every item, in key order
  for (const dreamor::Ref<Item> item : items.refs()) {
    every.push_back(item);
  }
  // --8<-- [end:at-hand]

  DREAMOR_CHECK(slow.size() == 3 && slow[0] == items.at("bracket") && slow[1] == items.at("frame") &&
                slow[2] == items.at("plate"));
  DREAMOR_CHECK(every.size() == 5 && every[0] == items.at("bolt") && &*every[4] == &items[4]);
}

void refer(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  // The data region (model.hpp) fills items and freezes it before the machines take Refs from it.
  DREAMOR_CHECK(items.frozen() && machines.frozen());
  const dreamor::Ref<Machine> m1 = machines.at("M1");
  const dreamor::Ref<Machine> m2 = machines.at("M2");
  DREAMOR_CHECK(m1->items.size() == 3 && m1->items[0] == items.at("frame") && m1->items[1] == items.at("plate") &&
                m1->items[2] == items.at("bracket"));
  DREAMOR_CHECK(machines.size() == 2 && machines[0].name == "M1" && machines[1].name == "M2");
  DREAMOR_CHECK(m2->items.size() == 3 && m2->items[0] == items.at("plate") && m2->items[1] == items.at("bolt") &&
                m2->items[2] == items.at("nut"));
  for (const Machine& machine : machines) {
    for (const dreamor::Ref<Item> item : machine.items) {
      DREAMOR_CHECK(&*item == &*items.at(item->name));  // every Ref of a machine, each to the item that items holds
    }
  }
}

void read(const Plant& p) {
  const dreamor::EntityVector<Machine>& machines = p.machines;

  // --8<-- [start:read]
  const dreamor::Ref<Machine> m1 = machines.at("M1");
  const double hours             = m1->capacity;
  const Machine& machine         = *m1;
  const std::string line         = std::format("{}: {} hours, {} first", m1, hours, machine.items.front());
  // --8<-- [end:read]
  DREAMOR_CHECK(exactlyEqual(hours, 4.0) && &machine == &machines.front());  // M1 comes first in key order
  DREAMOR_CHECK(line == "M1: 4 hours, frame first");
}

void compositeKey(const Plant& p) {
  const dreamor::EntityVector<Machine>& machines = p.machines;

  // --8<-- [start:composite-key]
  dreamor::EntityVector<Fixture> fixtures;
  fixtures.insert(Fixture{machines.at("M1"), "F1"});
  fixtures.insert(Fixture{machines.at("M2"), "F1"});
  fixtures.sort().freeze();

  const dreamor::Ref<Fixture> fixture = fixtures.at({machines.at("M2"), "F1"});
  const std::string label             = std::format("{}", fixture);  // M2_F1
  // --8<-- [end:composite-key]
  DREAMOR_CHECK(fixtures.size() == 2 && fixture->machine == machines.at("M2") && fixture->name == "F1");
  DREAMOR_CHECK(label == "M2_F1");
}

}  // namespace

int main() try {
  holdOnce();
  const Plant p = plant();
  lookUp(p);
  atHand(p);
  refer(p);
  read(p);
  compositeKey(p);
  return ::report("guide/entities");
} catch (...) { return ::uncaught(); }
