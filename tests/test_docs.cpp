// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The usage examples in the headers' comments, as written there. If a header's example
// changes, change it here too.
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
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

using cFrom    = Col<const Node*, "from">;
using cTo      = Col<const Node*, "to">;
using cNode    = Col<const Node*, "node">;
using cMachine = Col<const Machine*, "machine">;
using cItem    = Col<const Item*, "item">;
using cOrder   = Col<const Order*, "order">;
using cPeriod  = Col<std::uint32_t, "period">;

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
  const auto items_of   = [](cMachine m) { return m->items; };
  const auto fits       = [](cMachine, cItem) { return true; };
  const auto order_fits = [](cItem, cOrder) { return true; };
  const auto early      = [](cItem i) { return i->dueDate() < 100; };
  const auto reachable  = [](cFrom i, cTo j) { return i->id != j->id; };

  // index_set.hpp
  const auto sMACHINE_ITEMS = makeSet<cMachine, cItem>(machines, items_of).where(fits).collect();
  const auto sASSIGN        = makeSet<cOrder>(sMACHINE_ITEMS, orders).where(order_fits).collect();
  const auto sEARLY         = sASSIGN.where(early);
  DREAMOR_CHECK(sMACHINE_ITEMS.size() == 3 && sASSIGN.size() == 3 && sEARLY.size() == 2);
  const std::vector<std::uint32_t> periods{0, 1};
  const auto sOPEN = makeSet<cItem, cPeriod>(items, periods)
                         .where([](cItem i, cPeriod t) { return i->due > 100 || t.get() == 0; })
                         .collect();
  DREAMOR_CHECK(makeSet<>(sASSIGN, sOPEN).collect().size() == 4);

  // algebra.hpp
  const auto sUNIQUE   = makeSet<cMachine, cItem>(machines, items_of).collect();
  const auto in_supply = sMACHINE_ITEMS.membership();
  DREAMOR_CHECK(sUNIQUE.size() == 3 && in_supply(cMachine{machines[1]}, cItem{items[0]}) &&
                !in_supply(cMachine{machines[1]}, cItem{items[1]}));
  const auto sITEMS        = makeSet<cItem>(items).collect();
  const auto name_of       = [](cItem i) { return i->name; };
  const auto sBY_NAME      = sITEMS.orderBy(name_of);
  const auto sBY_NAME_DOWN = sITEMS.orderBy(name_of, dreamor::descending);
  DREAMOR_CHECK(std::get<cItem>(*sBY_NAME.begin())->name == "I1");
  DREAMOR_CHECK(std::get<cItem>(*sBY_NAME_DOWN.begin())->name == std::get<cItem>(*std::prev(sBY_NAME.end()))->name);
  const auto sEDGES = makeSet<cFrom, cTo>(Nodes, Nodes).where([](cFrom i, cTo j) { return i->id < j->id; }).collect();
  const auto sREVERSED = sEDGES.select([](cFrom i, cTo j) { return std::tuple<cFrom, cTo>{j, i}; });
  const auto sNODES    = makeSet<cNode>(Nodes).collect();
  const auto sFROM     = sNODES.select([](cNode n) { return cFrom{n}; });
  DREAMOR_CHECK(sREVERSED.size() == 3 && std::get<cFrom>(*sREVERSED.begin())->id == 2 &&
                unite(sEDGES, sREVERSED).size() == 6 && sFROM.size() == 3);

  // concert.hpp (VarTable)
  const auto sARCS = makeSet<cFrom, cTo>(Nodes, Nodes).where(reachable).collect();
  const VarTable<cFrom, cTo> vUse{env, sARCS, 0, 1, ILOBOOL, "Use"};
  const VarTable<cFrom, cTo> vFlow{
      env, sARCS, 0, [](cFrom i, cTo j) { return static_cast<double>(i->id + j->id); }, ILOFLOAT, "Flow"};
  const VarTable<cOrder> vQty{env, makeSet<cOrder>(orders).collect(), 0, [](cOrder o) { return o->quantity(); }};
  DREAMOR_CHECK(vUse.size() == 6 && std::string{vUse(cFrom{Nodes[0]}, cTo{Nodes[1]}).getName()} == "Use_1_2");
  DREAMOR_CHECK(exactlyEqual(vQty(cOrder{orders[0]}).getUB(), 5));
  for (const auto& [row, useVar] : vUse) {
    DREAMOR_CHECK(useVar.getImpl() != nullptr && vUse.contains(row));
  }
  const auto flowPlan = vFlow.solution(FakeCplex{env});
  DREAMOR_CHECK(exactlyEqual(flowPlan(cFrom{Nodes[0]}, cTo{Nodes[2]}), 4));
  const auto outArcs = sARCS.groupBy<cFrom>();
  DREAMOR_CHECK(outArcs.size() == 3);

  // concert.hpp (sumOf)
  const auto use_of = [&vUse](cFrom i, cTo j) { return vUse(i, j); };
  for (const auto& [node, arcs] : outArcs) {
    DREAMOR_CHECK(sumOf(env, arcs, use_of).build().expr_->terms.size() == 2);
  }

  // value_table.hpp (data on a set) and index_set/ordered.hpp (within groups)
  {
    using Tup            = std::tuple<cMachine, cPeriod>;
    const auto sRUN      = makeSet<cMachine, cPeriod>(machines, periods).collect();
    const auto weight_of = [](cPeriod t) { return 8.0 + t.get(); };
    const ValueTable<cMachine, cPeriod> weight{sRUN, weight_of};
    const auto periodsOfMachine = sRUN.ordered<cMachine>();
    const auto& [m, t]          = *sRUN.begin();
    const Tup* next             = periodsOfMachine.next(m, t);
    DREAMOR_CHECK(next != nullptr && std::get<cMachine>(*next) == m &&
                  exactlyEqual(weight(*next), 8.0 + std::get<cPeriod>(*next).get()));
  }

  // index_set/name_token.hpp (a column under std::format)
  {
    const Node& n1 = nodeData[0];
    const Node& n2 = nodeData[1];
    const cFrom i{&n1};
    const cTo j{&n2};
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
