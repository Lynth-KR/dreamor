// SPDX-License-Identifier: Apache-2.0
// The code in README.md and docs/guide.md, as written there (with a stand-in IloCplex and small data).
// If that code changes, change it here too. The comments below name the section it comes from.
#include <algorithm>
#include <cmath>
#include <cstddef>
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

using MachineCol = dreamor::Col<const Machine*, "machine">;
using ItemCol    = dreamor::Col<const Item*, "item">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;
using NodeCol    = dreamor::Col<const Node*, "node">;
using FromCol    = dreamor::Col<const Node*, "from">;
using ToCol      = dreamor::Col<const Node*, "to">;

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
  using type = std::tuple<MachineCol, PeriodCol>;
};

namespace {

struct FakeCplex {  // stand-in for solution(): every variable "solves" to its upper bound, capped
  IloEnv env;
  [[nodiscard]] IloEnv getEnv() const { return env; }
  static void getValues(IloNumArray out, const IloNumVarArray vars) {
    for (IloInt i = 0; i < vars.getSize(); ++i) out.add(std::min(vars[i].getUB(), 7.0));
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
  const auto items_of = [](MachineCol m) { return m->items; };
  {
    const auto sPRODUCE = dreamor::makeSet<MachineCol, ItemCol, PeriodCol>(machines, items_of, periods).collect();
    const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
    const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
        env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
    const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
    for (const auto& [machine, period, rows] : producedByMachinePeriod)
      model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
    DREAMOR_CHECK(sPRODUCE.size() == 9 && model.cs_->size() == 6);
    model.cs_->clear();
  }

  // --- Quick start
  const auto sPERIODS = dreamor::makeSet<PeriodCol>(periods).collect();
  const auto sROUTES  = dreamor::makeSet<MachineCol, ItemCol>(machines, items_of).collect();
  const auto sPRODUCE = dreamor::makeSet<PeriodCol>(sROUTES, periods).collect();
  const auto sSTOCK   = dreamor::makeSet<ItemCol, PeriodCol>(items, periods).collect();

  const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
      env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"};
  const dreamor::concert::VarTable<ItemCol, PeriodCol> vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"};
  const auto machine_usage        = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
  const auto orderedPeriods       = sPERIODS.ordered();
  const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
  for (const auto& [i, t] : sSTOCK) {
    IloExpr totalInFlow = dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build();
    if (const auto* prevPeriod = orderedPeriods.prev(t)) totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
    const double currentDemand = i->demand[t.get()];
    const auto endingStock     = vStock(i, t);
    model.add(totalInFlow - currentDemand == endingStock);
    totalInFlow.end();
  }
  const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
  for (const auto& [machine, period, rows] : producedByMachinePeriod) {
    IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
    model.add(totalLoad <= machine->capacity);
    totalLoad.end();
  }
  for (const auto& [row, yieldVar] : vYield) model.add(yieldVar <= yieldVar.getUB() * vSetup(row));
  IloExpr holdingCost =
      dreamor::concert::sumOf(env, sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
  IloExpr setupCost = dreamor::concert::sumOf(env, sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) {
                        return m->setup * vSetup(m, i, t);
                      }).build();
  IloExpr totalCost = holdingCost + setupCost;
  model.add(IloMinimize(env, totalCost));
  holdingCost.end();
  setupCost.end();
  totalCost.end();
  DREAMOR_CHECK(sSTOCK.size() == 9 && sPRODUCE.size() == 9);
  DREAMOR_CHECK(model.cs_->size() == 9 + 6 + 9);  // balance for every (item, period), capacity, setup links
  DREAMOR_CHECK(std::string{vYield(MachineCol{&m0}, ItemCol{&i1}, PeriodCol{2}).getName()} == "Yield_M0_I1_2");
  DREAMOR_CHECK(exactlyEqual(vYield(MachineCol{&m1}, ItemCol{&i1}, PeriodCol{0}).getUB(), 20));
  const auto yieldPlan = vYield.solution(FakeCplex{env});

  std::size_t printed = 0;
  for (const auto& [row, qty] : yieldPlan)
    if (qty > 1e-6) ++printed;
  DREAMOR_CHECK(printed == 9);

  // --- EntityVector
  {
    dreamor::EntityVector<Machine> plant;
    const auto [first, inserted] = plant.emplace(Machine{"M1", 40.0, 5.0, {}});
    const auto sRUN              = dreamor::makeSet<MachineCol, PeriodCol>(plant, periods).collect();
    plant.modify(&*first, [](Machine& m) { m.setup = 4.0; });
    DREAMOR_CHECK(inserted && !plant.emplace(Machine{"M1", 1.0, 1.0, {}}).second && sRUN.size() == periods.size() &&
                  exactlyEqual(plant.front().setup, 4.0));
  }

  // --- makeSet: entity / smart-pointer containers, predicates
  std::vector<Machine> machineObjects{m0, m1};
  std::vector<std::unique_ptr<Machine>> machineOwners;
  machineOwners.push_back(std::make_unique<Machine>(m1));
  DREAMOR_CHECK(std::get<MachineCol>(*dreamor::makeSet<MachineCol>(machineObjects).collect().begin()).get() ==
                machineObjects.data());
  DREAMOR_CHECK(std::get<MachineCol>(*dreamor::makeSet<MachineCol>(machineOwners).collect().begin()).get() ==
                machineOwners[0].get());
  const auto is_open = [](ItemCol i, PeriodCol t) { return i->demand[t.get()] > 0; };
  DREAMOR_CHECK(dreamor::makeSet<PeriodCol>(sROUTES, periods).where(is_open).collect().size() == 6);
  const auto sOPEN = dreamor::makeSet<ItemCol, PeriodCol>(items, periods).where(is_open).collect();
  DREAMOR_CHECK(dreamor::makeSet<>(sROUTES, sOPEN).collect().size() == 6);

  // --- Steering the visit order (sOPEN is the one just above)
  const auto fits_batch   = [](MachineCol m, ItemCol i) { return 30 * i->time <= m->capacity; };
  const auto sMAKE        = dreamor::makeSet<MachineCol, ItemCol, PeriodCol>(machines, items, periods)
                                .where(fits_batch)
                                .where(is_open)
                                .collect();
  const auto sMAKE_STAGED = dreamor::makeSet<MachineCol>(machines, sOPEN).where(fits_batch).collect();
  static_assert(std::is_same_v<decltype(sMAKE), decltype(sMAKE_STAGED)>);
  DREAMOR_CHECK(sMAKE.size() == 9 && sMAKE_STAGED.size() == sMAKE.size() &&
                std::ranges::all_of(sMAKE_STAGED, sMAKE.membership()));
  const auto sOPEN_ITEMS = sOPEN.select<ItemCol>();
  DREAMOR_CHECK(
      std::ranges::equal(dreamor::makeSet<>(sROUTES, sOPEN_ITEMS).collect(), sROUTES.where(sOPEN_ITEMS.membership())));

  // --- Deriving sets (orderedPeriods is the one from Quick start)
  const auto is_peak       = [](ItemCol i, PeriodCol t) { return i->demand[t.get()] >= 15; };
  const auto sPEAK_PRODUCE = sPRODUCE.where(is_peak);
  const auto sMACHINE_ITEM = sPRODUCE.select<MachineCol, ItemCol>();
  const auto in_routes     = sROUTES.membership();
  DREAMOR_CHECK(sPEAK_PRODUCE.size() == 3 && sMACHINE_ITEM.size() == 3);
  DREAMOR_CHECK(in_routes(MachineCol{&m0}, ItemCol{&i0}) && !in_routes(MachineCol{&m1}, ItemCol{&i0}) &&
                in_routes(*sROUTES.begin()));
  {  // a scope of its own: the README's t, apart from the lambdas' t in the sections below
    const PeriodCol t{1};
    if (const auto* prevPeriod = orderedPeriods.prev(t)) DREAMOR_CHECK(std::get<PeriodCol>(*prevPeriod).get() == 0);
    const auto* wrapped = orderedPeriods.nextCyclic(PeriodCol{2});
    DREAMOR_CHECK(std::get<PeriodCol>(orderedPeriods.first()).get() == 0 && orderedPeriods.isLast(PeriodCol{2}) &&
                  wrapped != nullptr && std::get<PeriodCol>(*wrapped).get() == 0);
    DREAMOR_CHECK(orderedPeriods.ord(PeriodCol{2}) == 2 && orderedPeriods.next(PeriodCol{0}, 2) != nullptr &&
                  orderedPeriods.next(PeriodCol{0}, 3) == nullptr && orderedPeriods.members().size() == 3);
  }
  {  // select(expr), over a graph of its own
    const std::vector<Node> nodes{{0}, {1}, {2}};
    const auto sNODES = dreamor::makeSet<NodeCol>(nodes).collect();
    const auto sEDGES = dreamor::makeSet<FromCol, ToCol>(nodes, nodes)
                            .where([](FromCol i, ToCol j) { return i->id + 1 == j->id; })
                            .collect();
    // (j,i) for every (i,j)
    const auto sREVERSED = sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; });
    const auto sTAILS    = sNODES.select([](NodeCol n) { return FromCol{n}; });  // the nodes as arc tails
    DREAMOR_CHECK(sREVERSED.size() == 2 && std::get<FromCol>(*sREVERSED.begin())->id == 1 &&
                  std::get<ToCol>(*sREVERSED.begin())->id == 0 && sTAILS.size() == 3);
  }

  // --- Grouping
  const auto producingRows = producedByItemPeriod.slice(ItemCol{&i1}, PeriodCol{1});
  std::size_t walked       = 0;
  for (const auto& [item, period, rows] : sPRODUCE.groupBy<ItemCol, PeriodCol>()) walked += rows.size();
  DREAMOR_CHECK(walked == sPRODUCE.size());
  DREAMOR_CHECK(producingRows.size() == 2 && producedByItemPeriod.slice(ItemCol{&i2}, PeriodCol{0}).empty());
  DREAMOR_CHECK(producedByItemPeriod.contains(ItemCol{&i0}, PeriodCol{0}) &&
                producedByItemPeriod.sliceSize(ItemCol{&i1}, PeriodCol{0}) == 2);
  DREAMOR_CHECK(producedByItemPeriod.regrouped().size() == sPRODUCE.size() && !producedByItemPeriod.at(0).empty() &&
                !producedByItemPeriod.empty());

  // --- VarTable: tightening bounds later, semi-continuous
  const auto in_maintenance = [](MachineCol m, PeriodCol t) { return m->name == "M0" && t.get() == 2; };
  for (const auto& row : vYield.keys().where(in_maintenance)) vYield(row).setUB(0);
  DREAMOR_CHECK(exactlyEqual(vYield(MachineCol{&m0}, ItemCol{&i0}, PeriodCol{2}).getUB(), 0));
  const auto& sBATCH = sPRODUCE;
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vBatch{
      dreamor::concert::semiContinuous, env, sBATCH, 5, 20, ILOFLOAT, "Batch"};
  DREAMOR_CHECK(vBatch(MachineCol{&m0}, ItemCol{&i0}, PeriodCol{0}).getImpl()->semicont);

  // --- Sums and constraints
  const auto setup_cost      = [&](MachineCol m, ItemCol i, PeriodCol t) { return m->setup * vSetup(m, i, t); };
  const double B             = 100;
  const IloRange setupBudget = (dreamor::concert::sumOf(env, sPRODUCE, setup_cost).build() <= B);
  setupBudget.setName("SetupBudget");
  model.add(setupBudget);

  // --- Reading solutions (after tightening)
  const auto tightenedPlan = vYield.solution(FakeCplex{env});
  DREAMOR_CHECK(isExactlyZero(tightenedPlan(MachineCol{&m0}, ItemCol{&i0}, PeriodCol{2})) &&
                tightenedPlan.contains(MachineCol{&m1}, ItemCol{&i1}, PeriodCol{1}));
  DREAMOR_CHECK(tightenedPlan.size() == 9 && tightenedPlan.keys().size() == 9);

  // --- Rules 9, Col
  DREAMOR_CHECK(dreamor::makeSet<MachineCol, PeriodCol>(machines, periods).where(MyFilter{}).collect().size() == 6);
  DREAMOR_CHECK(MachineCol::kName == "machine" && dreamor::keyName(MachineCol{&m0}) == "M0" &&
                dreamor::keyName(PeriodCol{7}) == 7);
  std::unordered_map<MachineCol, std::size_t> perMachine{{MachineCol{&m0}, 1}};
  DREAMOR_CHECK(perMachine.at(MachineCol{&m0}) == 1 && MachineCol{} == MachineCol{nullptr});
  return ::report("readme");
} catch (...) {
  return ::uncaught();
}
