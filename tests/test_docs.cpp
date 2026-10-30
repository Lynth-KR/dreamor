// SPDX-License-Identifier: Apache-2.0
// The usage examples in the headers' comments, as written there. If a header's example
// changes, change it here too.
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger/logger_light.hpp"
#include "float_compare.hpp"
#include "ilconcert/ilomodel.h"

using namespace dreamor;
using namespace dreamor::concert;

namespace {

struct Node {
  std::uint32_t id;
};
struct Item {
  std::string name;
  std::uint32_t due;
  [[nodiscard]] std::uint32_t dueDate() const { return due; }
};
struct Machine {
  std::string name;
  std::vector<const Item*> items;
};
struct Order {
  std::string name;
  double qty;
  [[nodiscard]] double quantity() const { return qty; }
};
inline std::uint32_t keyName(const Node* n) {
  return n->id;
}
inline const std::string& keyName(const Order* o) {
  return o->name;
}
}  // namespace

using FromCol    = Col<const Node*, "from">;
using ToCol      = Col<const Node*, "to">;
using NodeCol    = Col<const Node*, "node">;
using MachineCol = Col<const Machine*, "machine">;
using ItemCol    = Col<const Item*, "item">;
using OrderCol   = Col<const Order*, "order">;
using PeriodCol  = Col<std::uint32_t, "period">;

namespace {

struct FakeCplex {
  IloEnv env;
  [[nodiscard]] IloEnv getEnv() const { return env; }
  static void getValues(IloNumArray out, const IloNumVarArray vars) {
    for (IloInt i = 0; i < vars.getSize(); ++i) {
      out.add(vars[i].getUB());
    }
  }
};

}  // namespace

int main() try {
  const IloEnv env;
  std::vector<Node> nodeData{{1}, {2}, {3}};
  std::vector<const Node*> Nodes{nodeData.data(), &nodeData[1], &nodeData[2]};
  std::vector<Item> itemData{{"I1", 50}, {"I2", 150}};
  std::vector<const Item*> items{itemData.data(), &itemData[1]};
  std::vector<Machine> machineData{{"M1", items}, {"M2", {items[0]}}};
  std::vector<const Machine*> machines{machineData.data(), &machineData[1]};
  std::vector<Order> orderData{{"O1", 5}};
  std::vector<const Order*> orders{orderData.data()};
  const auto items_of   = [](MachineCol m) { return m->items; };
  const auto fits       = [](MachineCol, ItemCol) { return true; };
  const auto order_fits = [](ItemCol, OrderCol) { return true; };
  const auto early      = [](ItemCol i) { return i->dueDate() < 100; };
  const auto reachable  = [](FromCol i, ToCol j) { return i->id != j->id; };

  // index_set.hpp
  const auto sMACHINE_ITEMS = makeSet<MachineCol, ItemCol>(machines, items_of).where(fits).collect();
  const auto sASSIGN        = makeSet<OrderCol>(sMACHINE_ITEMS, orders).where(order_fits).collect();
  const auto sEARLY         = sASSIGN.where(early);
  DREAMOR_CHECK(sMACHINE_ITEMS.size() == 3 && sASSIGN.size() == 3 && sEARLY.size() == 2);
  const std::vector<std::uint32_t> periods{0, 1};
  const auto sOPEN = makeSet<ItemCol, PeriodCol>(items, periods)
                         .where([](ItemCol i, PeriodCol t) { return i->due > 100 || t.get() == 0; })
                         .collect();
  DREAMOR_CHECK(makeSet<>(sASSIGN, sOPEN).collect().size() == 4);

  // algebra.hpp
  const auto sUNIQUE   = makeSet<MachineCol, ItemCol>(machines, items_of).collect();
  const auto in_supply = sMACHINE_ITEMS.membership();
  DREAMOR_CHECK(sUNIQUE.size() == 3 && in_supply(MachineCol{machines[1]}, ItemCol{items[0]}) &&
                !in_supply(MachineCol{machines[1]}, ItemCol{items[1]}));
  const auto sITEMS   = makeSet<ItemCol>(items).collect();
  const auto sBY_NAME = sITEMS.orderBy([](const auto& row) { return std::get<ItemCol>(row)->name; });
  DREAMOR_CHECK(std::get<ItemCol>(*sBY_NAME.begin())->name == "I1");
  const auto sEDGES =
      makeSet<FromCol, ToCol>(Nodes, Nodes).where([](FromCol i, ToCol j) { return i->id < j->id; }).collect();
  const auto sREVERSED = sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; });
  const auto sNODES    = makeSet<NodeCol>(Nodes).collect();
  const auto sFROM     = sNODES.select([](NodeCol n) { return FromCol{n}; });
  DREAMOR_CHECK(sREVERSED.size() == 3 && std::get<FromCol>(*sREVERSED.begin())->id == 2 &&
                unite(sEDGES, sREVERSED).size() == 6 && sFROM.size() == 3);

  // concert.hpp (VarTable)
  const auto sARCS = makeSet<FromCol, ToCol>(Nodes, Nodes).where(reachable).collect();
  const VarTable<FromCol, ToCol> vUse{env, sARCS, 0, 1, ILOBOOL, "Use"};
  const VarTable<FromCol, ToCol> vFlow{
      env, sARCS, 0, [](FromCol i, ToCol j) { return static_cast<double>(i->id + j->id); }, ILOFLOAT, "Flow"};
  const VarTable<OrderCol> vQty{env, makeSet<OrderCol>(orders).collect(), 0, [](OrderCol o) { return o->quantity(); }};
  DREAMOR_CHECK(vUse.size() == 6 && std::string{vUse(FromCol{Nodes[0]}, ToCol{Nodes[1]}).getName()} == "Use_1_2");
  DREAMOR_CHECK(exactlyEqual(vQty(OrderCol{orders[0]}).getUB(), 5));
  for (const auto& [row, useVar] : vUse) {
    DREAMOR_CHECK(useVar.getImpl() != nullptr && vUse.contains(row));
  }
  const auto flowPlan = vFlow.solution(FakeCplex{env});
  DREAMOR_CHECK(exactlyEqual(flowPlan(FromCol{Nodes[0]}, ToCol{Nodes[2]}), 4));
  const auto outArcs = sARCS.groupBy<FromCol>();
  DREAMOR_CHECK(outArcs.size() == 3);

  // concert.hpp (sumOf)
  const auto use_of = [&vUse](FromCol i, ToCol j) { return vUse(i, j); };
  for (const auto& [node, arcs] : outArcs) {
    DREAMOR_CHECK(sumOf(env, arcs, use_of).build().expr_->terms.size() == 2);
  }

  // value_table.hpp (data on a set) and index_set/ordered.hpp (within groups)
  {
    using Tup                  = std::tuple<MachineCol, PeriodCol>;
    const auto sRUN            = makeSet<MachineCol, PeriodCol>(machines, periods).collect();
    const auto available_hours = [](PeriodCol t) { return 8.0 + t.get(); };
    const ValueTable<MachineCol, PeriodCol> hours{sRUN, available_hours};
    const auto periodsOfMachine = sRUN.ordered<MachineCol>();
    const auto& [m, t]          = *sRUN.begin();
    const Tup* next             = periodsOfMachine.next(m, t);
    DREAMOR_CHECK(next != nullptr && std::get<MachineCol>(*next) == m &&
                  exactlyEqual(hours(*next), 8.0 + std::get<PeriodCol>(*next).get()));
  }

  // index_set/name_token.hpp (a column under std::format)
  {
    const Node& n1 = nodeData[0];
    const Node& n2 = nodeData[1];
    const FromCol i{&n1};
    const ToCol j{&n2};
    DREAMOR_CHECK(std::format("Cap_{}_{}", i, j) == "Cap_1_2");
    // index_set/name_token.hpp (exprName)
    const IloModel model(env);
    const IloNumVar load(env, 0, 10);
    model.add(load <= 5).setName(dreamor::exprName("Capacity", i, j));
    DREAMOR_CHECK(std::string{dreamor::exprName("Capacity", i, j)} == "Capacity_1_2");
  }

  // logger/logger_light.hpp (FileSink): the lines go to the file for the scope, then back
  {
    const std::filesystem::path logFile = std::filesystem::temp_directory_path() / "dreamor_test_docs_filesink.log";
    std::filesystem::remove(logFile);
    {
      const dreamor::logger::FileSink sink(logFile.string().c_str());  // opens, redirects
      DREAMOR_CHECK(sink.isOpen());
      DREAMOR_LOG_INFO("started");
    }  // flushed, previous target restored, file closed
    std::ifstream in(logFile);
    bool logged = false;
    for (std::string line; std::getline(in, line);) {
      logged = logged || line.find("started") != std::string::npos;
    }
    in.close();  // Windows does not remove a file that is open
    DREAMOR_CHECK(logged && dreamor::logger::getOutput() == stderr);
    std::filesystem::remove(logFile);
  }
  return report("docs");
} catch (...) { return ::uncaught(); }
