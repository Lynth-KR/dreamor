// SPDX-License-Identifier: MIT-0
//
// docs/guide/columns.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>

#include "docs/guide/model.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;

// --8<-- [start:enum-text-columns]
enum class Shift { kDay, kNight };

DREAMOR_VALUE_COLUMN(cShift, Shift);           // the shift a machine runs in
DREAMOR_VALUE_COLUMN(cSupplier, std::string);  // who supplies a bought item, by name
// --8<-- [end:enum-text-columns]

// --8<-- [start:own-type]
struct Hours {  // the hours a unit takes, as your data gives them
  double value;

  friend bool operator==(const Hours& a, const Hours& b) {  // exact: equal hours are one row
    return std::equal_to<>{}(a.value, b.value);
  }
};

// --8<-- [end:own-type]

}  // namespace

// std::hash and std::formatter are specialized at global scope, outside the anonymous namespace.
// --8<-- [start:own-type-hash]
template <>
struct std::hash<Hours> {
  std::size_t operator()(const Hours& h) const noexcept { return std::hash<double>{}(h.value); }
};

template <>
struct std::formatter<Hours> : std::formatter<double> {  // writes the value in a name

  auto format(const Hours& h, std::format_context& context) const {
    return std::formatter<double>::format(h.value, context);
  }
};

// --8<-- [end:own-type-hash]

namespace {

// --8<-- [start:own-type-column]
DREAMOR_VALUE_COLUMN(cHours, Hours);
// --8<-- [end:own-type-column]

// The model's columns: two entity columns over Machine and Item, a value column of int, and cParent and cChild,
// two more entity columns over Item.
static_assert(std::is_base_of_v<dreamor::EntityColumn<cMachine, Machine>, cMachine> &&
              std::is_base_of_v<dreamor::EntityColumn<cItem, Item>, cItem> &&
              std::is_base_of_v<dreamor::ValueColumn<cPeriod, int>, cPeriod> &&
              std::is_base_of_v<dreamor::EntityColumn<cParent, Item>, cParent> &&
              std::is_base_of_v<dreamor::EntityColumn<cChild, Item>, cChild>);
static_assert(std::is_same_v<cMachine::ReadType, dreamor::Ref<Machine>> && std::is_same_v<cPeriod::ReadType, int>);
static_assert(std::is_same_v<cItem::ReadType, dreamor::Ref<Item>> &&
              std::is_same_v<cParent::ReadType, dreamor::Ref<Item>> &&
              std::is_same_v<cChild::ReadType, dreamor::Ref<Item>>);
// Three columns over Item, three types.
static_assert(!std::is_same_v<cItem, cParent> && !std::is_same_v<cItem, cChild> && !std::is_same_v<cParent, cChild>);
static_assert(std::is_convertible_v<cMachine, dreamor::Ref<Machine>> && std::is_convertible_v<cPeriod, int>);
static_assert(std::is_convertible_v<cShift, Shift> && std::is_convertible_v<cSupplier, const std::string&>);

// Where C++ picks one type of two, a column and an int give it none: get() takes the int out.
template <typename Column>
constexpr bool canMax = requires(Column c) { std::max(c, 1); };
template <typename Column>
constexpr bool canPickOne = requires(bool b, Column c) { b ? c : 1; };
static_assert(!canMax<cPeriod> && !canPickOne<cPeriod>);

// Two columns over one entity are two types; one is made from the other only explicitly.
static_assert(!std::is_same_v<cParent, cChild> && !std::is_same_v<cItem, cChild>);
static_assert(std::is_constructible_v<cItem, cChild> && !std::is_convertible_v<cChild, cItem>);

void read(const Plant& p) {
  const dreamor::EntityVector<Machine>& machines = p.machines;

  // --8<-- [start:read]
  const cMachine m = machines.at("M1");
  const cPeriod t{2};
  const double hours = m->capacity;
  const cPeriod next = t + 1;
  const bool late    = t > 1;

  const int start = std::max(t.get(), 3);  // std::max deduces one type
  const int shown = late ? t.get() : 0;    // ?: takes one type
  // --8<-- [end:read]
  DREAMOR_CHECK(m == machines.at("M1") && exactlyEqual(hours, 4.0));
  DREAMOR_CHECK(next == 3 && late);
  DREAMOR_CHECK(start == 3 && shown == 2);
}

void twoColumns(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;

  // --8<-- [start:two-columns]
  const cParent parent = items.at("frame");
  const cChild child   = items.at("plate");
  const cItem item{child};  // the plate, as an item
  // --8<-- [end:two-columns]
  DREAMOR_CHECK(parent == items.at("frame") && child == items.at("plate"));  // a Ref converts to either column
  DREAMOR_CHECK(item == items.at("plate") && item == child && parent != child);
}

using MakeSet = dreamor::IndexSet<cMachine, cItem, cPeriod>;

MakeSet callable(const Plant& p) {
  using dreamor::index_set::relational::from;

  // --8<-- [start:callable]
  const auto items_of = [](cMachine m) -> const auto& { return m->items; };
  const auto fits     = [](const cItem& i, cMachine m) { return 8 * i->time <= m->capacity; };  // a batch of 8

  const auto sPERIODS = from<cPeriod>(dreamor::interval::closed(1, p.periods)).build();
  const auto sMAKE    = from<cMachine>(p.machines).lateral<cItem>(items_of).where(fits).cross(sPERIODS).build();
  // --8<-- [end:callable]
  static_assert(std::is_same_v<decltype(sPERIODS), const dreamor::IndexSet<cPeriod>>);
  DREAMOR_CHECK(sPERIODS.size() == 3 && sPERIODS.contains(1) && sPERIODS.contains(3));  // closed(1, 3): both ends
  static_assert(std::is_same_v<decltype(sMAKE), const MakeSet>);
  // A batch of 8 frames takes 8 hours, over M1's 4; 8 plates take 4 hours, over M2's 3. Left: M1's plate and
  // bracket, M2's bolt and nut, in each of 3 periods.
  const cMachine m1 = p.machines.at("M1");
  const cMachine m2 = p.machines.at("M2");
  DREAMOR_CHECK(sMAKE.size() == 12);
  DREAMOR_CHECK(!sMAKE.contains(m1, p.items.at("frame"), 1) && !sMAKE.contains(m2, p.items.at("plate"), 1));
  DREAMOR_CHECK(sMAKE.contains(m1, p.items.at("plate"), 1) && sMAKE.contains(m1, p.items.at("bracket"), 3));
  DREAMOR_CHECK(sMAKE.contains(m2, p.items.at("bolt"), 2) && sMAKE.contains(m2, p.items.at("nut"), 3));
  return sMAKE;
}

// One rule over two columns of items: reads names the column that each call gives the generic lambda.
void readsColumns(const Plant& p, const MakeSet& sMAKE) {
  const auto quantity = bomTable(p);

  // --8<-- [start:reads]
  const auto is_sold = [](const auto& item) { return std::ranges::max(item->demand) > 0; };  // a cItem or a cChild

  const auto sMAKE_sold     = sMAKE.where(dreamor::reads<cItem>(is_sold));             // rows whose item is sold
  const auto sBOM_part_sold = quantity.keys().where(dreamor::reads<cChild>(is_sold));  // parts that are sold too
  // --8<-- [end:reads]
  const auto frame = p.items.at("frame");
  const auto nut   = p.items.at("nut");
  // Sold: the frame (1, 2, 2) and the nut (5 in period 2). sMAKE has no frame row (a batch does not fit M1), so its
  // sold rows are the nut's on M2; the nut is the one part that customers also buy.
  DREAMOR_CHECK(sMAKE_sold.size() == 3 && sMAKE_sold.contains(p.machines.at("M2"), nut, 2));
  DREAMOR_CHECK(!sMAKE.contains(p.machines.at("M1"), frame, 1));
  DREAMOR_CHECK(sBOM_part_sold.size() == 1 && std::get<cChild>(*sBOM_part_sold.begin()) == nut);
}

// A row is a std::tuple of its columns: a structured binding names them all, std::get takes one by its type.
void oneColumn(const Plant& p, const MakeSet& sMAKE) {
  // --8<-- [start:one-column]
  const auto& row                     = *sMAKE.begin();        // M1, the plate, period 1
  const auto& [machine, item, period] = row;                   // every column, each by a name
  const cItem first                   = std::get<cItem>(row);  // one column, by its type

  const auto byMachinePeriod = sMAKE.groupBy<cMachine, cPeriod>();
  const cItem leadOnM2       = std::get<cItem>(byMachinePeriod.slice(p.machines.at("M2"), 1).front());  // the bolt
  // --8<-- [end:one-column]
  DREAMOR_CHECK(machine == p.machines.at("M1") && item == p.items.at("plate") && period == 1);
  DREAMOR_CHECK(first == p.items.at("plate") && leadOnM2 == p.items.at("bolt"));
}

void key(const Plant& p, const MakeSet& sMAKE) {
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const dreamor::EntityVector<Item>& items       = p.items;

  // --8<-- [start:key]
  const cMachine m2 = machines.at("M2");
  const cChild nut  = items.at("nut");

  const bool makesBolt = sMAKE.contains(m2, items.at("bolt"), 3);  // a column, a Ref, an int
  const bool makesNut  = sMAKE.contains(m2, nut, 3);               // cChild in cItem's place

  const auto byMachinePeriod = sMAKE.groupBy<cMachine, cPeriod>();
  const std::size_t onM2     = byMachinePeriod.slice(m2, 1).size();
  // --8<-- [end:key]
  DREAMOR_CHECK(makesBolt && makesNut);
  DREAMOR_CHECK(onM2 == 2);  // the bolt and the nut: a batch of 8 plates does not fit M2's hours
  for (const auto& [machine, item, period] : byMachinePeriod.slice(m2, 1)) {
    DREAMOR_CHECK(machine == m2 && period == 1 && (item == items.at("bolt") || item == items.at("nut")));
  }
  // A Ref, a plain int and a column of another name each name one column of sMAKE's key alone.
  using dreamor::internal::isKey;
  using MakeKey = std::tuple<cMachine, cItem, cPeriod>;
  static_assert(isKey<MakeKey, cMachine, dreamor::Ref<Item>, int>() && isKey<MakeKey, cMachine, cChild, int>());
}

void namedKey(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;
  const BomTable quantity                  = bomTable(p);

  // --8<-- [start:named-key]
  const dreamor::Ref<Item> frame = items.at("frame");
  const dreamor::Ref<Item> plate = items.at("plate");

  const double plates = quantity(cParent{frame}, cChild{plate});  // a frame takes 2 plates
  const bool listed   = quantity.contains(cParent{plate}, cChild{frame});
  // --8<-- [end:named-key]
  DREAMOR_CHECK(exactlyEqual(plates, 2.0) && !listed);
  // Both of the key's columns hold an Item: a Ref, or a column of another name, does not say which it fills.
  using dreamor::internal::isKey;
  using BomKey = std::tuple<cParent, cChild>;
  static_assert(!isKey<BomKey, dreamor::Ref<Item>, dreamor::Ref<Item>>() && !isKey<BomKey, cItem, cChild>());
  static_assert(isKey<BomKey, cParent, cChild>());
}

void enumAndText() {
  // --8<-- [start:enum-text]
  const cShift shift{Shift::kNight};
  const cSupplier supplier{"Acme"};

  const bool atNight  = shift == Shift::kNight;
  const bool fromAcme = supplier == "Acme";
  // --8<-- [end:enum-text]
  DREAMOR_CHECK(atNight && fromAcme);
  DREAMOR_CHECK(shift.get() == Shift::kNight && supplier.get() == std::string{"Acme"});
}

void ownType(const Plant& p) {
  using dreamor::index_set::relational::from;

  // --8<-- [start:own-type-sets]
  const auto with_hours = [](cItem i) { return std::tuple<cItem, cHours>{i, Hours{i->time}}; };
  const auto by_hours   = [](cHours h) { return h.get().value; };

  const auto sITEM_HOURS = from<cItem>(p.items).build().select(with_hours);  // each item with its hours
  const auto sSTEPS      = sITEM_HOURS.select<cHours>().orderBy(by_hours, dreamor::descending);  // each value once

  const auto itemsByHours = sITEM_HOURS.groupBy<cHours>();
  const std::size_t quick = itemsByHours.slice(Hours{0.1}).size();
  const std::string top   = dreamor::indexedName("Step", *sSTEPS.begin());
  // --8<-- [end:own-type-sets]
  static_assert(std::is_same_v<decltype(sITEM_HOURS), const dreamor::IndexSet<cItem, cHours>>);
  static_assert(std::is_same_v<decltype(sSTEPS), const dreamor::IndexSet<cHours>>);
  // Five items: the frame takes 1 hour, the plate and the bracket 0.5, the bolt and the nut 0.1.
  DREAMOR_CHECK(sITEM_HOURS.size() == 5);
  DREAMOR_CHECK(sITEM_HOURS.contains(p.items.at("bolt"), Hours{0.1}) &&
                sITEM_HOURS.contains(p.items.at("frame"), Hours{1.0}));
  // Three distinct values, the largest first.
  DREAMOR_CHECK(sSTEPS.size() == 3);
  auto step = sSTEPS.begin();
  DREAMOR_CHECK(std::get<cHours>(*step) == Hours{1.0});
  ++step;
  DREAMOR_CHECK(std::get<cHours>(*step) == Hours{0.5});
  ++step;
  DREAMOR_CHECK(std::get<cHours>(*step) == Hours{0.1});
  DREAMOR_CHECK(quick == 2);  // the bolt and the nut
  for (const auto& [item, hours] : itemsByHours.slice(Hours{0.1})) {
    DREAMOR_CHECK(item == p.items.at("bolt") || item == p.items.at("nut"));
  }
  DREAMOR_CHECK(top == "Step_1");  // 1.0 written as std::format writes it for {}
}

}  // namespace

int main() try {
  const Plant p = plant();
  read(p);
  twoColumns(p);
  const MakeSet sMAKE = callable(p);
  readsColumns(p, sMAKE);
  oneColumn(p, sMAKE);
  key(p, sMAKE);
  namedKey(p);
  enumAndText();
  ownType(p);
  return ::report("guide/columns");
} catch (...) { return ::uncaught(); }
