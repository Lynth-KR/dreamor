// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Each public part used once, the way a model uses it, against the Concert stub: sets, the set algebra,
// the aggregates, VarTable, SolutionTable, ParamTable, orderedWithin, names and the logger's FileSink.
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger.hpp"
#include "ilconcert/ilomodel.h"
#include "support/check.hpp"
#include "support/float_compare.hpp"

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

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Machine {
  std::string name;
  std::vector<Ref<Item>> items;

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Order {
  std::string name;
  double qty;

  [[nodiscard]] double quantity() const { return qty; }
};

inline std::uint32_t primaryKey(const Node& n) {
  return n.id;
}

inline const std::string& primaryKey(const Order& o) {
  return o.name;
}
}  // namespace

namespace {
DREAMOR_ENTITY_COLUMN(cFrom, Node);
DREAMOR_ENTITY_COLUMN(cTo, Node);
DREAMOR_ENTITY_COLUMN(cNode, Node);
DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_ENTITY_COLUMN(cItem, Item);
DREAMOR_ENTITY_COLUMN(cOrder, Order);
DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);
}  // namespace

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

// A value whose formatter throws: a std::exception, or something that is not one.
struct Throws {
  bool standard = true;
};

struct NotAnException {};

}  // namespace

template <>
struct std::formatter<Throws> {
  static constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

  template <typename FormatContext>
  static auto format(const Throws& value, FormatContext& context) -> decltype(context.out()) {
    if (value.standard) { throw std::runtime_error("bad datum"); }
    throw NotAnException{};  // NOLINT(bugprone-std-exception-baseclass): the case is a throw of what is not one
  }
};

int main() try {
  const IloEnv env;
  const auto entities = []<typename T>(std::vector<T> elements) {
    EntityVector<T> made;
    for (T& element : elements) {
      static_cast<void>(made.insert(std::move(element)));
    }
    made.freeze();
    return made;
  };
  const EntityVector<Node> Nodes = entities(std::vector<Node>{{1}, {2}, {3}});
  const EntityVector<Item> items = entities(std::vector<Item>{{"I1", 50}, {"I2", 150}});
  const EntityVector<Machine> machines =
      entities(std::vector<Machine>{{"M1", {items.at("I1"), items.at("I2")}}, {"M2", {items.at("I1")}}});
  const EntityVector<Order> orders = entities(std::vector<Order>{{"O1", 5}});
  const auto items_of              = [](cMachine m) { return m->items; };
  const auto fits                  = [](cMachine, cItem) { return true; };
  const auto order_fits            = [](cItem, cOrder) { return true; };
  const auto early                 = [](cItem i) { return i->dueDate() < 100; };
  const auto reachable             = [](cFrom i, cTo j) { return i->id != j->id; };

  // sets, from a chain and derived
  const auto sMACHINE_ITEMS =
      index_set::relational::from<cMachine>(machines).lateral<cItem>(items_of).where(fits).build();
  const auto sASSIGN = index_set::relational::from(sMACHINE_ITEMS).cross<cOrder>(orders).where(order_fits).build();
  const auto sEARLY  = sASSIGN.where(early);
  DREAMOR_CHECK(sMACHINE_ITEMS.size() == 3 && sASSIGN.size() == 3 && sEARLY.size() == 2);
  const std::vector<std::uint32_t> periods{0, 1};
  const auto sOPEN = index_set::relational::from<cItem>(items)
                         .cross<cPeriod>(periods)
                         .where([](cItem i, cPeriod t) { return i->due > 100 || t.get() == 0; })
                         .build();
  DREAMOR_CHECK(index_set::relational::from(sASSIGN).join(sOPEN).build().size() == 4);

  // the set algebra
  const auto sUNIQUE   = index_set::relational::from<cMachine>(machines).lateral<cItem>(items_of).build();
  const auto in_supply = sMACHINE_ITEMS.membership();
  DREAMOR_CHECK(sUNIQUE.size() == 3 && in_supply(cMachine{machines.at("M2")}, cItem{items.at("I1")}) &&
                !in_supply(cMachine{machines.at("M2")}, cItem{items.at("I2")}));
  const auto sITEMS        = index_set::relational::from<cItem>(items).build();
  const auto name_of       = [](cItem i) { return i->name; };
  const auto sBY_NAME      = sITEMS.orderBy(name_of);
  const auto sBY_NAME_DOWN = sITEMS.orderBy(name_of, dreamor::descending);
  DREAMOR_CHECK(std::get<cItem>(*sBY_NAME.begin())->name == "I1");
  DREAMOR_CHECK(std::get<cItem>(*sBY_NAME_DOWN.begin())->name == std::get<cItem>(*std::prev(sBY_NAME.end()))->name);
  const auto sEDGES    = index_set::relational::from<cFrom>(Nodes)
                             .cross<cTo>(Nodes)
                             .where([](cFrom i, cTo j) { return i->id < j->id; })
                             .build();
  const auto sREVERSED = sEDGES.select([](cFrom i, cTo j) { return std::tuple<cFrom, cTo>{j, i}; });
  const auto sNODES    = index_set::relational::from<cNode>(Nodes).build();
  const auto sFROM     = sNODES.select([](cNode n) { return cFrom{n}; });
  DREAMOR_CHECK(sREVERSED.size() == 3 && std::get<cFrom>(*sREVERSED.begin())->id == 2 &&
                unite(sEDGES, sREVERSED).size() == 6 && sFROM.size() == 3);

  // the aggregates
  {
    const EntityVector<Order> orderObjects = entities(std::vector<Order>{{"o1", 2.0}, {"o2", 5.0}, {"o3", 4.0}});
    const auto sORDERS                     = index_set::relational::from<cOrder>(orderObjects).build();
    const auto quantity_of                 = [](cOrder o) { return o->quantity(); };
    const auto due_by_7                    = [](cItem i) { return i->dueDate() <= 7; };
    const auto nDue                        = dreamor::countIf(sITEMS, due_by_7);  // sITEMS from above
    DREAMOR_CHECK(nDue == sITEMS.where(due_by_7).size() && dreamor::allOf(sITEMS, due_by_7) == (nDue == sITEMS.size()));
    DREAMOR_CHECK(exactlyEqual(dreamor::maxOf(sORDERS, quantity_of), 5.0) &&
                  exactlyEqual(dreamor::sumValue(sORDERS, quantity_of), 11.0));
    const auto* biggest = dreamor::maxElement(sORDERS, quantity_of);
    DREAMOR_CHECK(biggest != nullptr && std::get<cOrder>(*biggest).get()->name == "o2");
  }

  // VarTable
  const auto sARCS = index_set::relational::from<cFrom>(Nodes).cross<cTo>(Nodes).where(reachable).build();
  const VarTable<cFrom, cTo> vUse{env, sARCS, 0, 1, ILOBOOL, "Use"};
  const auto cap_of = [](cFrom i, cTo j) { return static_cast<double>(i->id + j->id); };
  const VarTable<cFrom, cTo> vFlow{env, sARCS, 0, cap_of, ILOFLOAT, "Flow"};
  const auto quantity_of = [](cOrder o) { return o->quantity(); };
  const VarTable<cOrder> vQty{env, index_set::relational::from<cOrder>(orders).build(), 0, quantity_of};
  DREAMOR_CHECK(vUse.size() == 6 && std::string{vUse(cFrom{Nodes.at(1)}, cTo{Nodes.at(2)}).getName()} == "Use_1_2");
  DREAMOR_CHECK(exactlyEqual(vQty(cOrder{orders.at("O1")}).getUB(), 5));
  for (const auto& [row, useVar] : vUse) {
    DREAMOR_CHECK(useVar.getImpl() != nullptr && vUse.contains(row));
  }
  const auto flowPlan = vFlow.solution(FakeCplex{env});
  DREAMOR_CHECK(exactlyEqual(flowPlan(cFrom{Nodes.at(1)}, cTo{Nodes.at(3)}), 4));
  // SolutionTable
  double flowSum = 0;
  for (const auto& [row, flow] : flowPlan) {
    flowSum += flow;
  }
  DREAMOR_CHECK(exactlyEqual(flowSum, 24));
  const auto outArcs = sARCS.groupBy<cFrom>();
  DREAMOR_CHECK(outArcs.size() == 3);

  // sumExpr over a group
  const auto use_of = [&vUse](cFrom i, cTo j) { return vUse(i, j); };
  for (const auto& [node, arcs] : outArcs) {
    DREAMOR_CHECK(sumExpr(env, arcs, use_of).build().expr_->terms.size() == 2);
  }

  // ParamTable (data on a set) and orderedWithin
  {
    using Tup            = std::tuple<cMachine, cPeriod>;
    const auto sRUN      = index_set::relational::from<cMachine>(machines).cross<cPeriod>(periods).build();
    const auto weight_of = [](cPeriod t) { return 8.0 + t.get(); };
    const ParamTable<double, cMachine, cPeriod> weight{sRUN, weight_of};
    const ParamTable<double, cMachine, cPeriod> hours{sRUN, 8.0};
    const VarTable<cMachine, cPeriod> vRun{env, sRUN, 0, hours};
    const auto runPlan = vRun.solution(FakeCplex{env});
    const ParamTable<double, cMachine, cPeriod> carried{sRUN, runPlan};
    DREAMOR_CHECK(exactlyEqual(carried(*sRUN.begin()), 8.0));
    const auto periodsOfMachine = sRUN.orderedWithin<cMachine>();
    const auto& [m, t]          = *sRUN.begin();
    const Tup* next             = periodsOfMachine.next(m, t);
    DREAMOR_CHECK(next != nullptr && std::get<cMachine>(*next) == m &&
                  exactlyEqual(weight(*next), 8.0 + std::get<cPeriod>(*next).get()));
  }

  // a column under std::format
  {
    const cFrom i{Nodes.at(1)};
    const cTo j{Nodes.at(2)};
    DREAMOR_CHECK(std::format("Cap_{}_{}", i, j) == "Cap_1_2");
    // indexedName
    const IloModel model(env);
    const IloNumVar load(env, 0, 10);
    model.add(load <= 5).setName(dreamor::indexedName("Capacity", i, j));
    DREAMOR_CHECK(std::string{dreamor::indexedName("Capacity", i, j)} == "Capacity_1_2");
  }

  // FileSink: the lines go to the file for the scope, then back
  {
    const std::filesystem::path logFile = std::filesystem::temp_directory_path() / "dreamor_test_usage_filesink.log";
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

  // the logger's own failures: a line too long is cut and marked, a formatter that throws leaves a line that says
  // so, and a file that cannot be opened leaves the lines where they went, with an error line there
  {
    const std::filesystem::path logFile = std::filesystem::temp_directory_path() / "dreamor_test_usage_failures.log";
    const std::filesystem::path nowhere = std::filesystem::temp_directory_path() / "dreamor_no_such_folder";
    std::filesystem::remove(logFile);
    std::filesystem::remove_all(nowhere);
    {
      const dreamor::logger::FileSink sink(logFile.string().c_str());
      DREAMOR_CHECK(sink.isOpen());
      DREAMOR_LOG_INFO("{}", std::string(3000, 'x'));
      DREAMOR_LOG_INFO("{}", Throws{.standard = true});
      DREAMOR_LOG_INFO("{}", Throws{.standard = false});
      const FILE* const target = dreamor::logger::getOutput();
      const dreamor::logger::FileSink lost((nowhere / "lost.log").string().c_str());
      DREAMOR_CHECK(!lost.isOpen() && dreamor::logger::getOutput() == target);
    }
    std::ifstream in(logFile);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
      lines.push_back(line);
    }
    in.close();  // Windows does not remove a file that is open
    DREAMOR_CHECK(lines.size() == 4);
    // a line holds at most the buffer's 1024 bytes, the newline among them
    DREAMOR_CHECK(!lines.empty() && lines[0].size() == 1023 && lines[0].ends_with("...[TRUNCATED]"));
    DREAMOR_CHECK(lines.size() > 1 && lines[1].find("<log formatting threw: bad datum>") != std::string::npos);
    DREAMOR_CHECK(lines.size() > 2 &&
                  lines[2].find("<log formatting threw a non-std::exception>") != std::string::npos);
    DREAMOR_CHECK(lines.size() > 3 && lines[3].find("FileSink: failed to open") != std::string::npos);
    std::filesystem::remove(logFile);
  }
  return report("docs");
} catch (...) { return ::uncaught(); }
