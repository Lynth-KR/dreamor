// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Every allocation of a run through the set layer made to fail in turn: entities filled, sorted and frozen, sets made
// by chains and by set-builder notation, the set algebra, groups, positions, membership, a ParamTable, a Concert
// VarTable and sums. Each run either ends with the results of a run without a failure, or throws std::bad_alloc, and
// then what survives it holds what the reference promises: an EntityVector whose sort failed is as it was, a chain
// or a set-builder expression whose build() failed builds again, and every set made before is unchanged.
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"
#include "support/failing_allocation.hpp"

namespace {

struct Item {
  std::string name;
  int due = 0;

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Machine {
  std::uint32_t id = 0;
  double capacity  = 0;
  std::vector<dreamor::Ref<Item>> items;

  [[nodiscard]] std::uint32_t primaryKey() const { return id; }
};

struct MachineRecord {
  std::uint32_t id = 0;
  double capacity  = 0;
  std::vector<std::string> items;
};

struct Demand {
  std::string item;
  int period = 0;
  double qty = 0;
};

DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_ENTITY_COLUMN(cFrom, Machine);
DREAMOR_ENTITY_COLUMN(cTo, Machine);
DREAMOR_VALUE_COLUMN(cPeriod, int);
DREAMOR_VALUE_COLUMN(cLabel, std::string);

constexpr int kLast = 3;

// A name past the small-string buffer, so that copying it allocates.
constexpr std::string_view kLongName = "a sprocket whose name does not fit in the small-string buffer";

// The input, prepared before any allocation is made to fail. "gear" comes twice, and machine 1 twice: the second is
// not inserted. The parts are enough that each index grows past its first size while it is filled.
constexpr int kParts = 30;

struct Data {
  std::vector<Item> items{{"gear", 1}, {"widget", 2}, {std::string{kLongName}, 3}, {"bolt", 2}, {"gear", 9}};
  std::vector<MachineRecord> machines{
      {3, 60.0, {"gear", "bolt"}},
      {1, 100.0, {"widget", "gear", std::string{kLongName}}},
      {2, 80.0, {"bolt"}},
      {1, 5.0, {}},
  };
  std::vector<int> periods{1, 2, 3};
  std::vector<Demand> demand{
      {"gear", 1, 5.0},
      {"bolt", 2, 3.0},
      {"widget", 3, 7.5},
      {std::string{kLongName}, 1, 2.0},
      {"gear", 3, 1.0},
  };

  Data() {
    for (int k = 0; k < kParts; ++k) {
      std::string name = std::format("part {:02}", k);
      if (k % 5 == 0) { name += " with a name that does not fit in the small-string buffer"; }
      items.push_back({name, 1 + (k % 3)});
      machines[1].items.push_back(name);
      if (k % 4 == 0) { demand.push_back({name, 1 + (k % 3), 0.5 * k}); }
    }
  }
};

struct ItemsOf {
  const std::vector<dreamor::Ref<Item>>& operator()(cMachine m) const { return m->items; }
};

struct Fits {
  bool operator()(cMachine m, cItem i) const { return m->capacity >= 30.0 * i->due; }
};

struct FitsBatch {
  bool operator()(cMachine m, cPeriod t) const { return m->capacity >= 25.0 * t.get(); }
};

struct Open {
  bool operator()(cItem i, cPeriod t) const { return t.get() >= i->due; }
};

struct Linked {
  bool operator()(cFrom a, cTo b) const { return b->id == a->id + 1; }
};

struct LabelOf {
  std::tuple<cItem, cLabel> operator()(cItem i, cPeriod t) const {
    return {i, cLabel{std::format("{} in period {}", i->name, t.get())}};
  }
};

struct DueOf {
  int operator()(cItem i) const { return i->due; }
};

struct ByCapacityDown {
  bool operator()(const Machine& a, const Machine& b) const { return a.capacity > b.capacity; }
};

struct DemandRow {
  const dreamor::EntityVector<Item>* items;

  std::tuple<cItem, cPeriod> operator()(const Demand& d) const { return {items->at(d.item), d.period}; }
};

struct QtyOf {
  double operator()(const Demand& d) const { return d.qty; }
};

struct HoursOf {
  double operator()(cMachine m, cItem i, cPeriod t) const { return m->capacity / (i->due * t.get()); }
};

using RoutesSet = dreamor::IndexSet<cMachine, cItem>;
using PlanSet   = dreamor::IndexSet<cItem, cPeriod>;
using MakeSet   = dreamor::IndexSet<cMachine, cItem, cPeriod>;
using MakeTable = dreamor::concert::VarTable<cMachine, cItem, cPeriod>;

struct MakeVar {
  const MakeTable* table;

  IloNumVar operator()(cMachine m, cItem i, cPeriod t) const { return (*table)(m, i, t); }
};

struct Early {
  bool operator()(cPeriod t) const { return t.get() < kLast; }
};

[[nodiscard]] auto openChain(const dreamor::EntityVector<Item>& items, const std::vector<int>& periods) {
  return dreamor::index_set::relational::from<cItem>(items).cross<cPeriod>(periods).where(Open{});
}

[[nodiscard]] auto supplyOf(const RoutesSet& routes, const PlanSet& demand) {
  using namespace dreamor::index_set::set_builder;
  return of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(routes), in<cItem, cPeriod>(demand));
}

using OpenChain =
    decltype(openChain(std::declval<const dreamor::EntityVector<Item>&>(), std::declval<const std::vector<int>&>()));
using SupplyOf  = decltype(supplyOf(std::declval<const RoutesSet&>(), std::declval<const PlanSet&>()));
using Groups    = decltype(std::declval<const MakeSet&>().groupBy<cMachine>());
using Positions = decltype(std::declval<const PlanSet&>().ordered());
using Within    = decltype(std::declval<const PlanSet&>().orderedWithin<cItem>());
using InRoutes  = decltype(std::declval<const RoutesSet&>().membership());

using Lines = std::vector<std::string>;

template <typename Tup>
[[nodiscard]] std::string rowText(const Tup& row) {
  std::string text;
  std::apply([&text](const auto&... column) { ((text += std::format("{}|", column)), ...); }, row);
  return text;
}

// A set's rows in order, each found again by contains(), by its membership and by a ParamTable on it: a lookup index
// that a failed allocation left half built would lose a row here.
template <typename... Columns>
[[nodiscard]] Lines setLines(const dreamor::IndexSet<Columns...>& set) {
  Lines lines;
  const dreamor::ParamTable<int, Columns...> ones{set, 1};
  const auto in_set = set.membership();
  for (const auto& row : set) {
    const bool found = std::apply(
        [&](const Columns&... key) {
          return set.contains(key...) && in_set(key...) && ones.contains(key...) && ones(key...) == 1;
        },
        row);
    lines.push_back(rowText(row) + (found ? "" : " (not found)"));
  }
  lines.push_back(std::format("{} rows", set.size()));
  return lines;
}

[[nodiscard]] Lines itemLines(const dreamor::EntityVector<Item>& items) {
  Lines lines;
  for (const Item& item : items) {
    const bool found = items.contains(item.name) && &*items.at(item.name) == &item;
    lines.push_back(std::format("{} {}{}", item.name, item.due, found ? "" : " (not found)"));
  }
  return lines;
}

[[nodiscard]] Lines machineLines(const dreamor::EntityVector<Machine>& machines) {
  Lines lines;
  for (const Machine& machine : machines) {
    std::string line = std::format("{} {}", machine.id, machine.capacity);
    const bool found = machines.contains(machine.id) && &*machines.at(machine.id) == &machine;
    for (const dreamor::Ref<Item>& item : machine.items) {
      line += " " + item->name;
    }
    lines.push_back(line + (found ? "" : " (not found)"));
  }
  return lines;
}

// The items as inserting the first `count` records leaves them: each key once, in the order of insertion.
[[nodiscard]] Lines insertedItems(const Data& data, std::size_t count) {
  dreamor::EntityVector<Item> expected;
  for (std::size_t i = 0; i < count; ++i) {
    static_cast<void>(expected.insert(data.items[i]));
  }
  expected.freeze();
  return itemLines(expected);
}

// The machines as inserting the first `count` records leaves them, their items found in `items`.
[[nodiscard]] Lines insertedMachines(const Data& data, const dreamor::EntityVector<Item>& items, std::size_t count) {
  dreamor::EntityVector<Machine> expected;
  for (std::size_t i = 0; i < count; ++i) {
    Machine machine{data.machines[i].id, data.machines[i].capacity, {}};
    for (const std::string& name : data.machines[i].items) {
      machine.items.push_back(items.at(name));
    }
    static_cast<void>(expected.insert(std::move(machine)));
  }
  expected.freeze();
  return machineLines(expected);
}

// The values a run reads, at most.
constexpr std::size_t kSeen = 4096;

enum class Phase : std::uint8_t { kFillItems, kSortItems, kFillMachines, kSortMachines, kSets };

// One run: what it made, and the values it read while it ran. A value is read while an allocation may fail, so that
// a lookup that builds an index on its first use is made to fail too; it is kept in an array that does not allocate.
struct Run {
  const Data* data;
  IloEnv env;
  Phase phase                   = Phase::kFillItems;
  std::size_t items_inserted    = 0;  // the insert calls that returned
  std::size_t machines_inserted = 0;
  dreamor::EntityVector<Item> items;
  dreamor::EntityVector<Machine> machines;
  std::optional<RoutesSet> routes;
  std::optional<RoutesSet> routes_of;
  std::optional<OpenChain> open_chain;
  std::optional<PlanSet> open;
  std::optional<MakeSet> make;
  std::optional<PlanSet> demand;
  std::optional<MakeSet> supply;
  std::optional<SupplyOf> supply_of;
  std::optional<MakeSet> supply_built;
  std::optional<dreamor::IndexSet<cMachine>> busy;
  std::optional<dreamor::IndexSet<cItem, cLabel>> labels;
  std::optional<PlanSet> supplied;
  std::optional<PlanSet> either;
  std::optional<PlanSet> both;
  std::optional<PlanSet> only_open;
  std::optional<PlanSet> by_due;
  std::optional<MakeSet> make_supplied;
  std::optional<dreamor::IndexSet<cFrom, cTo>> links;
  std::optional<dreamor::IndexSet<cFrom, cTo>> reach;
  std::optional<MakeSet> moved;
  std::optional<Groups> groups;
  std::optional<MakeSet> regrouped;
  std::optional<Positions> positions;
  std::optional<Within> within;
  std::optional<InRoutes> in_routes;
  std::optional<dreamor::ParamTable<double, cItem, cPeriod>> qty;
  std::optional<dreamor::ParamTable<double, cMachine, cItem, cPeriod>> hours;
  std::optional<MakeTable> vars;
  std::array<std::int64_t, kSeen> seen{};
  std::size_t seen_count = 0;

  explicit Run(const Data& input) : data{&input} {}

  Run(const Run&)            = delete;
  Run& operator=(const Run&) = delete;
  Run(Run&&)                 = delete;
  Run& operator=(Run&&)      = delete;

  ~Run() { env.end(); }

  // A number as 64 bits: a double by its bits, so that it compares exactly.
  template <typename T>
  void see(T value) {
    if (seen_count == seen.size()) { return; }
    if constexpr (std::is_floating_point_v<T>) {
      seen[seen_count++] = std::bit_cast<std::int64_t>(static_cast<double>(value));
    } else {
      seen[seen_count++] = static_cast<std::int64_t>(value);
    }
  }

  void go() {
    namespace rel = dreamor::index_set::relational;
    using namespace dreamor::index_set::set_builder;

    for (const Item& item : data->items) {
      see(items.insert(item));
      ++items_inserted;
    }
    phase = Phase::kSortItems;
    static_cast<void>(items.sort());
    items.freeze();

    phase = Phase::kFillMachines;
    for (const MachineRecord& record : data->machines) {
      Machine machine{record.id, record.capacity, {}};
      for (const std::string& name : record.items) {
        machine.items.push_back(items.at(name));
      }
      see(machines.insert(std::move(machine)));
      ++machines_inserted;
    }
    phase = Phase::kSortMachines;
    static_cast<void>(machines.sort(ByCapacityDown{}));
    machines.freeze();

    phase = Phase::kSets;
    routes.emplace(rel::from<cMachine>(machines).lateral<cItem>(ItemsOf{}).where(Fits{}).build());
    routes_of.emplace(of<cMachine, cItem>(in<cMachine>(machines), in<cItem>(ItemsOf{})).where(Fits{}).build());
    open_chain.emplace(openChain(items, data->periods));
    open.emplace(open_chain->build());
    make.emplace(
        of<cMachine, cItem, cPeriod>(in<cMachine, cItem>(*routes), in<cPeriod>(dreamor::interval::closed(1, kLast)))
            .where(FitsBatch{})
            .build());
    demand.emplace(data->demand, DemandRow{&items});
    supply.emplace(rel::from(*routes).join(*demand).build());
    supply_of.emplace(supplyOf(*routes, *demand));
    supply_built.emplace(supply_of->build());
    busy.emplace(supply->select<cMachine>());
    labels.emplace(open->select(LabelOf{}));
    supplied.emplace(supply->select<cItem, cPeriod>());
    either.emplace(dreamor::unite(*supplied, *open));
    both.emplace(dreamor::intersect(*open, *supplied));
    only_open.emplace(dreamor::difference(*open, *supplied));
    by_due.emplace(open->orderBy(DueOf{}, dreamor::descending));
    make_supplied.emplace(make->where(supplied->membership()));
    links.emplace(rel::from<cFrom>(machines).cross<cTo>(machines).where(Linked{}).build());
    reach.emplace(dreamor::transitiveClosure(*links));
    {
      MakeSet copy = *make;
      moved.emplace(std::move(copy));
      see(copy.empty());  // NOLINT(bugprone-use-after-move): a moved-from set is empty, as the reference says
    }

    groups.emplace(make->groupBy<cMachine>());
    for (const Machine& machine : machines) {
      see(groups->contains(cMachine{machines.at(machine.id)}));
      see(groups->slice(cMachine{machines.at(machine.id)}).size());
    }
    regrouped.emplace(groups->regrouped());

    positions.emplace(open->ordered());
    within.emplace(open->orderedWithin<cItem>());
    for (const auto& [item, period] : *open) {
      see(positions->positionOf(item, period).value_or(99));
      const auto* const next = positions->next(item, period);
      see(next != nullptr ? positions->positionOf(std::get<0>(*next), std::get<1>(*next)).value_or(99) : 98);
      const auto* const later = within->next(item, period);
      see(later != nullptr ? std::get<cPeriod>(*later).get() : 0);
      see(within->positionOf(item, period).value_or(99));
      see(open->contains(item, period));
    }

    in_routes.emplace(routes->membership());
    for (const Machine& machine : machines) {
      for (const Item& item : items) {
        see((*in_routes)(cMachine{machines.at(machine.id)}, cItem{items.at(item.name)}));
      }
    }

    qty.emplace(data->demand, DemandRow{&items}, QtyOf{});
    for (const auto& [item, period] : *open) {
      see(qty->contains(item, period) ? (*qty)(item, period) : -1.0);
    }
    hours.emplace(*make, HoursOf{});

    vars.emplace(env, *make, 0, *hours, ILOFLOAT, "Make");
    for (const auto& [machine, item, period] : *supply) {
      see(vars->contains(machine, item, period) ? (*vars)(machine, item, period).getUB() : -1.0);
    }
    for (const auto& [machine, rows] : *groups) {
      const IloExpr sum = dreamor::concert::sumExpr(env, rows, MakeVar{&*vars}).build();
      see(sum.expr_->terms.size());
      const IloExpr early = dreamor::concert::sumExpr(env, rows, MakeVar{&*vars}).where(Early{}).build();
      see(early.expr_->terms.size());
    }
    const IloExpr total = dreamor::concert::sumExpr(env, *make_supplied, MakeVar{&*vars}).build();
    see(total.expr_->terms.size());
  }

  // What the run made, as text, for each object that exists. The entity vectors are left to check().
  [[nodiscard]] std::vector<std::pair<std::string, Lines>> made() const {
    std::vector<std::pair<std::string, Lines>> all;
    const auto add = [&all]<typename T>(const char* name, const std::optional<T>& object, auto lines_of) {
      if (object.has_value()) { all.emplace_back(name, lines_of(*object)); }
    };
    const auto set_lines = [](const auto& set) { return setLines(set); };
    add("routes", routes, set_lines);
    add("routes_of", routes_of, set_lines);
    add("open", open, set_lines);
    // A chain or a set-builder expression whose build() failed builds the same rows again.
    add("open_chain.build()", open_chain, [](const OpenChain& chain) { return setLines(chain.build()); });
    add("make", make, set_lines);
    add("demand", demand, set_lines);
    add("supply", supply, set_lines);
    add("supply_of.build()", supply_of, [](const SupplyOf& set_of) { return setLines(set_of.build()); });
    add("supply_built", supply_built, set_lines);
    add("busy", busy, set_lines);
    add("labels", labels, set_lines);
    add("supplied", supplied, set_lines);
    add("either", either, set_lines);
    add("both", both, set_lines);
    add("only_open", only_open, set_lines);
    add("by_due", by_due, set_lines);
    add("make_supplied", make_supplied, set_lines);
    add("links", links, set_lines);
    add("reach", reach, set_lines);
    add("moved", moved, set_lines);
    add("groups", groups, [](const Groups& grouped) {
      Lines lines;
      for (const auto& [machine, rows] : grouped) {
        lines.push_back(std::format("group {} of {}", machine, rows.size()));
        for (const auto& row : rows) {
          lines.push_back(rowText(row));
        }
        lines.push_back(std::format("slice {}", grouped.slice(machine).size()));
      }
      return lines;
    });
    add("regrouped", regrouped, set_lines);
    add("positions", positions, [](const Positions& ordered) {
      Lines lines;
      for (std::size_t i = 0; i < ordered.size(); ++i) {
        const auto& row      = ordered.at(i);
        const std::size_t at = ordered.positionOf(std::get<0>(row), std::get<1>(row)).value_or(99);
        lines.push_back(std::format("{} at {}", rowText(row), at));
      }
      return lines;
    });
    add("within", within, [](const Within& ordered) {
      Lines lines;
      for (const auto& row : ordered.members()) {
        const auto* const next = ordered.next(std::get<0>(row), std::get<1>(row));
        const std::size_t at   = ordered.positionOf(std::get<0>(row), std::get<1>(row)).value_or(99);
        lines.push_back(std::format("{} at {} then {}", rowText(row), at, next != nullptr ? rowText(*next) : "end"));
      }
      return lines;
    });
    add("qty", qty, [](const dreamor::ParamTable<double, cItem, cPeriod>& table) {
      Lines lines;
      for (const auto& [row, value] : table) {
        lines.push_back(std::format("{} {}", rowText(row), value));
      }
      return lines;
    });
    add("hours", hours, [](const dreamor::ParamTable<double, cMachine, cItem, cPeriod>& table) {
      Lines lines;
      for (const auto& [row, value] : table) {
        lines.push_back(std::format("{} {} {}", rowText(row), value, table(row)));
      }
      return lines;
    });
    add("vars", vars, [](const MakeTable& table) {
      Lines lines;
      for (const auto& [row, var] : table) {
        lines.push_back(std::format("{} {} {} {} {}",
                                    rowText(row),
                                    var.getName(),
                                    var.getLB(),
                                    var.getUB(),
                                    table(row).getImpl() == var.getImpl()));
      }
      return lines;
    });
    return all;
  }
};

// The results of the run without a failure.
struct Reference {
  Lines items;
  Lines machines;
  std::vector<std::pair<std::string, Lines>> made;
  std::array<std::int64_t, kSeen> seen{};
  std::size_t seen_count = 0;
};

void fail(std::size_t n, const char* what) {
  std::fprintf(stderr, "allocation %zu: %s\n", n, what);
  ++g_failures;
}

// Checks what survived a run whose allocation n threw (or that ended, if thrown is false).
void check(std::size_t n, Run& run, const Reference& reference, bool thrown) {
  ++g_checks;
  const std::size_t failures_before = g_failures;
  for (std::size_t i = 0; i < run.seen_count; ++i) {
    if (i >= reference.seen_count || run.seen[i] != reference.seen[i]) {
      fail(n, "a value read while the run went differs from the run without a failure");
      break;
    }
  }
  if (!thrown && run.seen_count != reference.seen_count) { fail(n, "the run read a different number of values"); }

  // The entity vectors, by how far the run got.
  if (run.phase == Phase::kFillItems) {
    // An insert that throws keeps none of the entity it was given, and the entities inserted before.
    run.items.freeze();
    if (itemLines(run.items) != insertedItems(*run.data, run.items_inserted)) {
      fail(n, "an EntityVector whose insert threw does not hold what was inserted before");
    }
  } else if (run.phase == Phase::kSortItems) {
    // The only allocation of sort() is the new index (the key is a reference, the comparison allocates nothing),
    // and if it cannot be allocated, the vector is left as it was.
    run.items.freeze();
    if (itemLines(run.items) != insertedItems(*run.data, run.data->items.size())) {
      fail(n, "an EntityVector whose sort() threw std::bad_alloc is not as it was");
    }
  } else if (itemLines(run.items) != reference.items) {
    fail(n, "the items differ from the run without a failure");
  }
  if (run.phase <= Phase::kSortItems) {
    if (!run.machines.empty()) { fail(n, "machines were inserted before the items were sorted"); }
  } else if (run.phase == Phase::kFillMachines) {
    run.machines.freeze();
    if (machineLines(run.machines) != insertedMachines(*run.data, run.items, run.machines_inserted)) {
      fail(n, "an EntityVector whose insert threw does not hold what was inserted before");
    }
  } else if (run.phase == Phase::kSortMachines) {
    run.machines.freeze();
    if (machineLines(run.machines) != insertedMachines(*run.data, run.items, run.data->machines.size())) {
      fail(n, "an EntityVector whose sort(comp) threw std::bad_alloc is not as it was");
    }
  } else if (machineLines(run.machines) != reference.machines) {
    fail(n, "the machines differ from the run without a failure");
  }

  // Every object that exists is the one the run without a failure made.
  const std::vector<std::pair<std::string, Lines>> made = run.made();
  if (!thrown && made.size() != reference.made.size()) { fail(n, "the run made a different number of objects"); }
  for (const auto& [name, lines] : made) {
    bool same = false;
    for (const auto& [reference_name, reference_lines] : reference.made) {
      if (reference_name == name) { same = reference_lines == lines; }
    }
    if (!same) {
      std::fprintf(stderr, "allocation %zu: %s differs from the run without a failure\n", n, name.c_str());
      ++g_failures;
    }
  }
  if (g_failures != failures_before) {
    std::fprintf(stderr, "allocation %zu: the run %s\n", n, thrown ? "threw std::bad_alloc" : "ended");
  }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the terminate handler names the failing run
std::size_t g_armed_at = 0;

[[noreturn]] void terminated() noexcept {
  std::fprintf(stderr, "allocation %zu: std::terminate, as from an exception out of a noexcept function\n", g_armed_at);
  std::fflush(stderr);
  std::abort();
}

}  // namespace

int main() try {
  std::set_terminate(terminated);
  const Data data;

  // Once to warm up: what a program allocates once (a logger's state, the stub's arena) is not counted in a run.
  { Run{data}.go(); }

  Reference reference;
  std::size_t count = 0;
  {
    Run run{data};
    failing_allocation::arm(0);
    run.go();
    count                = failing_allocation::disarm();
    reference.items      = itemLines(run.items);
    reference.machines   = machineLines(run.machines);
    reference.made       = run.made();
    reference.seen       = run.seen;
    reference.seen_count = run.seen_count;
  }
  DREAMOR_CHECK(count > 0 && reference.seen_count < reference.seen.size());

  std::size_t thrown_count = 0;
  std::size_t ended_count  = 0;
  for (std::size_t n = 1; n <= count; ++n) {
    g_armed_at = n;
    Run run{data};
    bool thrown = false;
    try {
      failing_allocation::arm(n);
      run.go();
      static_cast<void>(failing_allocation::disarm());
    } catch (const std::bad_alloc&) {
      static_cast<void>(failing_allocation::disarm());
      thrown = true;
    } catch (const std::exception& e) {
      static_cast<void>(failing_allocation::disarm());
      std::fprintf(stderr, "allocation %zu: threw %s, not std::bad_alloc\n", n, e.what());
      ++g_failures;
      continue;
    }
    if (!failing_allocation::fired()) {
      fail(n, "the run made fewer allocations than the run without a failure");
    } else if (!thrown) {
      ++ended_count;  // a failure that something caught, and then went on
      std::printf("  allocation %zu: thrown and caught, and the run ended\n", n);
    } else {
      ++thrown_count;
    }
    check(n, run, reference, thrown);
  }
  g_armed_at = 0;
  std::printf("allocation_failure: %zu allocations, %zu runs threw std::bad_alloc, %zu ended despite it\n",
              count,
              thrown_count,
              ended_count);
  return report("allocation_failure");
} catch (...) { return ::uncaught(); }
