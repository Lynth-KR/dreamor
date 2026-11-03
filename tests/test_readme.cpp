// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The code in README.md and docs/guide.md, as written there (with a stand-in IloCplex and small data).
// If that code changes, change it here too. The comments below name the section it comes from.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"
namespace {

struct Item {
  std::string name;
  double time, hold;
  std::vector<double> demand;
};
struct Machine {
  std::string name;
  double capacity, setup;
  std::vector<const Item*> items;
};
inline const std::string& keyName(const Item* i) {
  return i->name;
}
inline const std::string& keyName(const Machine* m) {
  return m->name;
}
inline const std::string& primaryKey(const Machine& m) {  // README EntityVector
  return m.name;
}
struct Node {
  std::size_t id;
};
}  // namespace

using cMachine = dreamor::Col<const Machine*, "machine">;
using cItem    = dreamor::Col<const Item*, "item">;
using cPeriod  = dreamor::Col<std::size_t, "period">;
using cNode    = dreamor::Col<const Node*, "node">;
using cFrom    = dreamor::Col<const Node*, "from">;
using cTo      = dreamor::Col<const Node*, "to">;

// README Rules, 9
namespace {

struct MyFilter {
  template <typename... Args>
  bool operator()(const Args&... /*args*/) const {
    return true;
  }
};
}  // namespace

template <>
struct dreamor::PredicateColumns<MyFilter> {
  using type = std::tuple<cMachine, cPeriod>;
};

namespace {

struct FakeCplex {  // stand-in for solution(): every variable "solves" to its upper bound, capped
  IloEnv env;
  [[nodiscard]] IloEnv getEnv() const { return env; }
  static void getValues(IloNumArray out, const IloNumVarArray vars) {
    for (IloInt i = 0; i < vars.getSize(); ++i) {
      out.add(std::min(vars[i].getUB(), 7.0));
    }
  }
};

// README Quick start: the model class, as written there
class LotSizing {
 public:
  LotSizing(IloEnv environment,
            const std::vector<const Machine*>& machines,
            const std::vector<const Item*>& items,
            const std::vector<std::size_t>& periods)
      : env(environment),
        model(environment),
        sPERIODS(dreamor::makeSet<cPeriod>(periods).collect()),
        sROUTES(dreamor::makeSet<cMachine, cItem>(machines, items_of).collect()),
        sPRODUCE(dreamor::makeSet<cPeriod>(sROUTES, periods).collect()),  // expand an existing set
        sSTOCK(dreamor::makeSet<cItem, cPeriod>(items, periods).collect()) {
    addInventoryBalance();
    addCapacity();
    addSetupLinks();
    addObjective();
  }

  IloEnv env;
  IloModel model;

  // sets
  const dreamor::SetOf<cPeriod> sPERIODS;
  const dreamor::SetOf<cMachine, cItem> sROUTES;
  const dreamor::SetOf<cMachine, cItem, cPeriod> sPRODUCE;
  const dreamor::SetOf<cItem, cPeriod> sSTOCK;

  // 4) Variables, over the sets above
  const dreamor::concert::VarTable<cMachine, cItem, cPeriod> vYield{
      env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
  const dreamor::concert::VarTable<cMachine, cItem, cPeriod> vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"};
  const dreamor::concert::VarTable<cItem, cPeriod> vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"};

 private:
  static constexpr auto items_of = [](cMachine m) {
    return m->items;
  };  // dependent source: returns a range of pointers
  static constexpr auto max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; };

  // 5) Constraints
  //    Inventory balance: for every (item, period). It must hold even when no machine makes the item in that
  //    period, so it iterates sSTOCK
  void addInventoryBalance() {
    const auto orderedPeriods       = sPERIODS.ordered();
    const auto producedByItemPeriod = sPRODUCE.groupBy<cItem, cPeriod>();
    for (const auto& [i, t] : sSTOCK) {
      IloExpr totalInFlow = dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build();
      if (const auto* prevPeriod = orderedPeriods.prev(t)) { totalInFlow += vStock(i, std::get<cPeriod>(*prevPeriod)); }
      const double currentDemand = i->demand[t.get()];
      const auto endingStock     = vStock(i, t);
      model.add(totalInFlow - currentDemand == endingStock);
      totalInFlow.end();
    }
  }
  //    Machine capacity, each row named the way the variables are (Capacity_M0_0)
  void addCapacity() {
    const auto machine_usage = [this](cMachine m, cItem i, cPeriod t) { return i->time * vYield(m, i, t); };
    for (const auto& [machine, period, rows] : sPRODUCE.groupBy<cMachine, cPeriod>()) {
      IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
      model.add(totalLoad <= machine->capacity).setName(dreamor::exprName("Capacity", machine, period));
      totalLoad.end();
    }
  }
  //    No production without setup
  void addSetupLinks() {
    for (const auto& [row, yieldVar] : vYield) {
      model.add(yieldVar <= yieldVar.getUB() * vSetup(row));
    }
  }

  // 6) Objective
  void addObjective() {
    IloExpr holdingCost =
        dreamor::concert::sumOf(env, sSTOCK, [this](cItem i, cPeriod t) { return i->hold * vStock(i, t); }).build();
    IloExpr setupCost = dreamor::concert::sumOf(env, sPRODUCE, [this](cMachine m, cItem i, cPeriod t) {
                          return m->setup * vSetup(m, i, t);
                        }).build();
    IloExpr totalCost = holdingCost + setupCost;
    model.add(IloMinimize(env, totalCost));
    holdingCost.end();
    setupCost.end();
    totalCost.end();
  }
};

}  // namespace

int main() try {
  const IloEnv env;
  const IloModel model(env);
  const Item i0{"I0", 1.0, 0.5, {10, 0, 20}};
  const Item i1{"I1", 2.0, 1.0, {0, 15, 5}};
  const Item i2{"I2", 1.5, 0.7, {5, 5, 5}};
  const Machine m0{"M0", 60, 30, {&i0, &i1}};
  const Machine m1{"M1", 40, 20, {&i1}};
  const std::vector<const Item*> items{&i0, &i1, &i2};
  const std::vector<const Machine*> machines{&m0, &m1};
  const std::vector<std::size_t> periods{0, 1, 2};

  // --- The example at the top
  const auto items_of = [](cMachine m) { return m->items; };
  {
    const auto sPRODUCE         = dreamor::makeSet<cMachine, cItem, cPeriod>(machines, items_of, periods).collect();
    const auto max_possible_qty = [](cMachine m, cItem i) { return m->capacity / i->time; };
    const dreamor::concert::VarTable<cMachine, cItem, cPeriod> vYield{
        env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
    const auto machine_usage           = [&](cMachine m, cItem i, cPeriod t) { return i->time * vYield(m, i, t); };
    const auto producedByMachinePeriod = sPRODUCE.groupBy<cMachine, cPeriod>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
    }
    DREAMOR_CHECK(sPRODUCE.size() == 9 && model.cs_->size() == 6);
    model.cs_->clear();
  }

  // --- Quick start
  const LotSizing lotSizing(env, machines, items, periods);
  const auto& sPERIODS             = lotSizing.sPERIODS;
  const auto& sROUTES              = lotSizing.sROUTES;
  const auto& sPRODUCE             = lotSizing.sPRODUCE;
  const auto& sSTOCK               = lotSizing.sSTOCK;
  const auto& vYield               = lotSizing.vYield;
  const auto& vSetup               = lotSizing.vSetup;
  const auto orderedPeriods        = sPERIODS.ordered();
  const auto producedByItemPeriod  = sPRODUCE.groupBy<cItem, cPeriod>();
  const auto* const quickStartRows = lotSizing.model.cs_;
  DREAMOR_CHECK(
      std::ranges::any_of(*quickStartRows, [](const IloConstraintI* c) { return c->name == "Capacity_M0_0"; }));
  DREAMOR_CHECK(sSTOCK.size() == 9 && sPRODUCE.size() == 9);
  DREAMOR_CHECK(quickStartRows->size() == 9 + 6 + 9);  // balance for every (item, period), capacity, setup links
  DREAMOR_CHECK(std::string{vYield(cMachine{&m0}, cItem{&i1}, cPeriod{2}).getName()} == "Yield_M0_I1_2");
  DREAMOR_CHECK(exactlyEqual(vYield(cMachine{&m1}, cItem{&i1}, cPeriod{0}).getUB(), 20));
  const auto yieldPlan = vYield.solution(FakeCplex{env});

  std::size_t printed = 0;
  for (const auto& [row, qty] : yieldPlan) {
    if (qty > 1e-6) { ++printed; }
  }
  DREAMOR_CHECK(printed == 9);

  // --- EntityVector
  {
    dreamor::EntityVector<Machine> plant;
    const bool inserted = plant.emplace(Machine{"M1", 40.0, 5.0, {}}).second;
    const bool again    = plant.emplace(Machine{"M1", 1.0, 1.0, {}}).second;
    plant.sort().freeze();
    const auto sRUN = dreamor::makeSet<cMachine, cPeriod>(plant, periods).collect();
    if (const Machine* found = plant.find("M1")) {
      plant.modify(found, [](Machine& m) { m.setup = 4.0; });
    }
    DREAMOR_CHECK(inserted && !again && sRUN.size() == periods.size() && exactlyEqual(plant.front().setup, 4.0));
  }

  // --- makeSet: entity / smart-pointer containers, predicates
  std::vector<Machine> machineObjects{m0, m1};
  std::vector<std::unique_ptr<Machine>> machineOwners;
  machineOwners.push_back(std::make_unique<Machine>(m1));
  DREAMOR_CHECK(std::get<cMachine>(*dreamor::makeSet<cMachine>(machineObjects).collect().begin()).get() ==
                machineObjects.data());
  DREAMOR_CHECK(std::get<cMachine>(*dreamor::makeSet<cMachine>(machineOwners).collect().begin()).get() ==
                machineOwners[0].get());
  const auto is_open = [](cItem i, cPeriod t) { return i->demand[t.get()] > 0; };
  DREAMOR_CHECK(dreamor::makeSet<cPeriod>(sROUTES, periods).where(is_open).collect().size() == 6);
  const auto sOPEN = dreamor::makeSet<cItem, cPeriod>(items, periods).where(is_open).collect();
  DREAMOR_CHECK(dreamor::makeSet<>(sROUTES, sOPEN).collect().size() == 6);

  // --- Steering the visit order (sOPEN is the one just above)
  const auto fits_batch = [](cMachine m, cItem i) { return 30 * i->time <= m->capacity; };
  const auto sMAKE =
      dreamor::makeSet<cMachine, cItem, cPeriod>(machines, items, periods).where(fits_batch).where(is_open).collect();
  const auto sMAKE_STAGED = dreamor::makeSet<cMachine>(machines, sOPEN).where(fits_batch).collect();
  static_assert(std::is_same_v<decltype(sMAKE), decltype(sMAKE_STAGED)>);
  DREAMOR_CHECK(sMAKE.size() == 9 && sMAKE_STAGED.size() == sMAKE.size() &&
                std::ranges::all_of(sMAKE_STAGED, sMAKE.membership()));
  const auto sOPEN_ITEMS = sOPEN.select<cItem>();
  DREAMOR_CHECK(
      std::ranges::equal(dreamor::makeSet<>(sROUTES, sOPEN_ITEMS).collect(), sROUTES.where(sOPEN_ITEMS.membership())));

  // --- Deriving sets (orderedPeriods is the one from Quick start)
  const auto is_peak       = [](cItem i, cPeriod t) { return i->demand[t.get()] >= 15; };
  const auto sPEAK_PRODUCE = sPRODUCE.where(is_peak);
  const auto sMACHINE_ITEM = sPRODUCE.select<cMachine, cItem>();
  const auto in_routes     = sROUTES.membership();
  DREAMOR_CHECK(sPEAK_PRODUCE.size() == 3 && sMACHINE_ITEM.size() == 3);
  DREAMOR_CHECK(in_routes(cMachine{&m0}, cItem{&i0}) && !in_routes(cMachine{&m1}, cItem{&i0}) &&
                in_routes(*sROUTES.begin()));
  {  // a scope of its own: the README's t, apart from the lambdas' t in the sections below
    const cPeriod t{1};
    if (const auto* prevPeriod = orderedPeriods.prev(t)) { DREAMOR_CHECK(std::get<cPeriod>(*prevPeriod).get() == 0); }
    const auto* wrapped = orderedPeriods.nextCyclic(cPeriod{2});
    DREAMOR_CHECK(std::get<cPeriod>(orderedPeriods.first()).get() == 0 && orderedPeriods.isLast(cPeriod{2}) &&
                  wrapped != nullptr && std::get<cPeriod>(*wrapped).get() == 0);
    DREAMOR_CHECK(orderedPeriods.ord(cPeriod{2}) == 2 && orderedPeriods.next(cPeriod{0}, 2) != nullptr &&
                  orderedPeriods.next(cPeriod{0}, 3) == nullptr && orderedPeriods.members().size() == 3);
  }
  {  // a row only where a neighbour is: the domain filtered, as the guide writes it
    std::size_t neighbourRows = 0;
    const auto not_first      = [&orderedPeriods](cPeriod t) { return !orderedPeriods.isFirst(t); };
    for (const auto& [i, t] : sSTOCK.where(not_first)) {  // {(i,t) in STOCK: ord(t) > 1}
      const cPeriod prevPeriod = std::get<cPeriod>(orderedPeriods.prevOf(t));
      DREAMOR_CHECK(i.get() != nullptr && prevPeriod.get() + 1 == t.get());
      ++neighbourRows;
    }
    DREAMOR_CHECK(neighbourRows == 6);  // 3 items, periods 1 and 2
  }
  {  // select(expr), over a graph of its own
    const std::vector<Node> nodes{{0}, {1}, {2}};
    const auto sNODES = dreamor::makeSet<cNode>(nodes).collect();
    const auto sEDGES =
        dreamor::makeSet<cFrom, cTo>(nodes, nodes).where([](cFrom i, cTo j) { return i->id + 1 == j->id; }).collect();
    // (j,i) for every (i,j)
    const auto sREVERSED = sEDGES.select([](cFrom i, cTo j) { return std::tuple<cFrom, cTo>{j, i}; });
    const auto sTAILS    = sNODES.select([](cNode n) { return cFrom{n}; });  // the nodes as arc tails
    DREAMOR_CHECK(sREVERSED.size() == 2 && std::get<cFrom>(*sREVERSED.begin())->id == 1 &&
                  std::get<cTo>(*sREVERSED.begin())->id == 0 && sTAILS.size() == 3);
  }

  // --- Grouping
  const auto producingRows = producedByItemPeriod.slice(cItem{&i1}, cPeriod{1});
  std::size_t walked       = 0;
  for (const auto& [item, period, rows] : sPRODUCE.groupBy<cItem, cPeriod>()) {
    walked += rows.size();
  }
  DREAMOR_CHECK(walked == sPRODUCE.size());
  DREAMOR_CHECK(producingRows.size() == 2 && producedByItemPeriod.slice(cItem{&i2}, cPeriod{0}).empty());
  DREAMOR_CHECK(producedByItemPeriod.contains(cItem{&i0}, cPeriod{0}) &&
                producedByItemPeriod.sliceSize(cItem{&i1}, cPeriod{0}) == 2);
  DREAMOR_CHECK(producedByItemPeriod.regrouped().size() == sPRODUCE.size() && !producedByItemPeriod.at(0).empty() &&
                !producedByItemPeriod.empty());

  // --- VarTable: tightening bounds later, semi-continuous
  const auto in_maintenance = [](cMachine m, cPeriod t) { return m->name == "M0" && t.get() == 2; };
  for (const auto& row : vYield.keys().where(in_maintenance)) {
    vYield(row).setUB(0);
  }
  DREAMOR_CHECK(exactlyEqual(vYield(cMachine{&m0}, cItem{&i0}, cPeriod{2}).getUB(), 0));
  const auto& sBATCH = sPRODUCE;
  const dreamor::concert::VarTable<cMachine, cItem, cPeriod> vBatch{
      dreamor::concert::semiContinuous, env, sBATCH, 5, 20, ILOFLOAT, "Batch"};
  DREAMOR_CHECK(vBatch(cMachine{&m0}, cItem{&i0}, cPeriod{0}).getImpl()->semicont);

  // --- Sums and constraints
  const auto setup_cost      = [&](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
  const double B             = 100;
  const IloRange setupBudget = (dreamor::concert::sumOf(env, sPRODUCE, setup_cost).build() <= B);
  setupBudget.setName("SetupBudget");
  model.add(setupBudget);

  // --- Reading solutions (after tightening)
  const auto tightenedPlan = vYield.solution(FakeCplex{env});
  DREAMOR_CHECK(isExactlyZero(tightenedPlan(cMachine{&m0}, cItem{&i0}, cPeriod{2})) &&
                tightenedPlan.contains(cMachine{&m1}, cItem{&i1}, cPeriod{1}));
  DREAMOR_CHECK(tightenedPlan.size() == 9 && tightenedPlan.keys().size() == 9);

  // --- Rules 9, Col
  DREAMOR_CHECK(dreamor::makeSet<cMachine, cPeriod>(machines, periods).where(MyFilter{}).collect().size() == 6);
  DREAMOR_CHECK(cMachine::kName == "machine" && dreamor::keyName(cMachine{&m0}) == "M0" &&
                dreamor::keyName(cPeriod{7}) == 7);
  std::unordered_map<cMachine, std::size_t> perMachine{{cMachine{&m0}, 1}};
  DREAMOR_CHECK(perMachine.at(cMachine{&m0}) == 1 && cMachine{} == cMachine{nullptr});
  return ::report("readme");
} catch (...) { return ::uncaught(); }
