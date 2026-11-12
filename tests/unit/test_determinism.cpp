// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Determinism (support/determinism.hpp) of the set layer and the Concert backend on the stub: a chain and set-builder
// notation, their visit order, projections with repeats and the set algebra, groups and their slices, an order and
// its neighbours, orderBy both ways, a transitive closure with a cycle, a ParamTable and a set from records, an
// EntityVector sorted by its text key; then the example's lot-sizing model as the stub holds it - each variable's name,
// bounds and type in the order made, each row's name, sense, right-hand side and terms in the order added - and the two
// sums of its cost. Each pass, after the heap is shaken from its seed, must print determinism.txt.
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <tuple>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "production_planning_concert.hpp"
#include "support/check.hpp"
#include "support/determinism.hpp"

namespace {

using planning::cItem;
using planning::cMachine;
using planning::cPeriod;

DREAMOR_ENTITY_COLUMN(cParent, planning::Item);
DREAMOR_ENTITY_COLUMN(cChild, planning::Item);

struct BomRecord {
  int parent;
  int child;
  double quantity;
};

class Text {
 public:
  void heading(std::string_view name) { text_ += std::format("== {}\n", name); }

  void line(const std::string& line) { text_ += line + '\n'; }

  template <typename Set>
  void rows(std::string_view name, const Set& set) {
    heading(std::format("{} ({} rows)", name, set.size()));
    for (const auto& row : set) {
      line(determinism::rowText(row));
    }
  }

  [[nodiscard]] const std::string& text() const { return text_; }

 private:
  std::string text_;
};

void setLayer(const planning::Instance& data, Text& out) {
  using dreamor::index_set::relational::from;
  using namespace dreamor::index_set::set_builder;
  const planning::Sets s{data};
  out.rows("sROUTES (set-builder, select)", s.sROUTES);
  out.rows("sPRODUCE", s.sPRODUCE);

  const auto demand_periods = [](cItem i) {
    std::vector<int> periods;
    for (std::size_t t = 0; t < i->demand.size(); ++t) {
      if (i->demand[t] > 0) { periods.push_back(static_cast<int>(t)); }
    }
    return periods;
  };
  const auto busy       = [](cMachine m, cPeriod t) { return m->capacity > 55 || t.get() != 1; };
  const auto chain      = from<cMachine>(s.machines).join(s.sROUTES).cross(s.sPERIODS).where(busy);
  const auto sCHAIN     = chain.build();
  const auto sDEMAND    = from<cItem>(s.items).lateral<cPeriod>(demand_periods).build();
  const auto sDEMAND_SB = of<cItem, cPeriod>(in<cItem>(s.items), in<cPeriod>(demand_periods)).build();
  const auto as_made    = [](cMachine /*m*/, cItem i, cPeriod t) { return std::tuple<cItem, cPeriod>{i, t}; };
  const auto sMADE      = sCHAIN.select(as_made);  // repeats dropped, the first kept
  out.line(std::format("visit order: {}", decltype(chain)::visitOrder()));
  out.rows("chain: machines join routes cross periods where busy", sCHAIN);
  out.rows("chain: items lateral demand periods", sDEMAND);
  out.rows("set-builder: items, demand periods", sDEMAND_SB);
  out.rows("select (item, period) of the chain", sMADE);
  out.rows("unite(demand, made)", dreamor::unite(sDEMAND, sMADE));
  out.rows("intersect(demand, made)", dreamor::intersect(sDEMAND, sMADE));
  out.rows("difference(demand, made)", dreamor::difference(sDEMAND, sMADE));
  out.rows("difference(made, demand)", dreamor::difference(sMADE, sDEMAND));

  out.heading("groupBy<cItem, cPeriod> of sPRODUCE");
  for (const auto& [i, t, rows] : s.sPRODUCE.groupBy<cItem, cPeriod>()) {
    out.line(std::format("{} {}: {} rows", i, t, rows.size()));
    for (const auto& row : rows) {
      out.line("  " + determinism::rowText(row));
    }
  }
  out.heading("groupBy<cMachine> of sCHAIN, slices by machine");
  const auto by_machine = sCHAIN.groupBy<cMachine>();
  for (const auto& machine : s.machines) {
    const auto slice = by_machine.slice(s.machines.ref(machine));
    out.line(std::format("{}: {} rows", machine.name, slice.size()));
    for (const auto& row : slice) {
      out.line("  " + determinism::rowText(row));
    }
  }

  out.heading("ordered periods: previous and next");
  const auto periods = s.sPERIODS.ordered();
  for (const auto& [t] : s.sPERIODS) {
    const auto* before = periods.prev(t);
    const auto* after  = periods.next(t);
    out.line(std::format("{}: {} {}",
                         t,
                         before != nullptr ? determinism::rowText(*before) : "-",
                         after != nullptr ? determinism::rowText(*after) : "-"));
  }
  const auto time_of = [](cMachine /*m*/, cItem i) { return i->time; };
  out.rows("sROUTES orderBy time", s.sROUTES.orderBy(time_of));
  out.rows("sROUTES orderBy time, descending", s.sROUTES.orderBy(time_of, dreamor::descending));

  // A bill of materials with a cycle, by the items' places.
  determinism::Draw draw{7};
  std::vector<BomRecord> bom;
  bom.reserve((2 * determinism::kItems) + 3);
  for (int k = 0; k < 2 * determinism::kItems; ++k) {
    bom.push_back(BomRecord{draw.below(determinism::kItems), draw.below(determinism::kItems), 1.0 + draw.below(4)});
  }
  bom.push_back(BomRecord{0, 1, 1.0});
  bom.push_back(BomRecord{1, 2, 1.0});
  bom.push_back(BomRecord{2, 0, 1.0});
  std::vector<BomRecord> distinct;  // a set from records takes no repeated row
  for (const BomRecord& r : bom) {
    bool seen = false;
    for (const BomRecord& d : distinct) {
      seen = seen || (d.parent == r.parent && d.child == r.child);
    }
    if (!seen) { distinct.push_back(r); }
  }
  const auto edge_of = [&s, &data](const BomRecord& r) {
    return std::tuple<cParent, cChild>{s.items.at(data.items[static_cast<std::size_t>(r.parent)].name),
                                       s.items.at(data.items[static_cast<std::size_t>(r.child)].name)};
  };
  const auto quantity_of = [](const BomRecord& r) { return r.quantity; };
  const dreamor::IndexSet<cParent, cChild> sBOM{distinct, edge_of};
  const dreamor::ParamTable<double, cParent, cChild> quantity{distinct, edge_of, quantity_of};
  out.rows("set from records: bom", sBOM);
  out.rows("transitiveClosure(bom)", dreamor::transitiveClosure(sBOM));
  out.heading("ParamTable from records: quantity");
  for (const auto& [row, value] : quantity) {
    out.line(std::format("{} = {}", determinism::rowText(row), value));
  }

  dreamor::EntityVector<planning::Item> sorted;
  for (const planning::ItemRecord& r : data.items) {
    static_cast<void>(
        sorted.emplace(planning::Item{.name = r.name, .time = r.time, .hold = r.hold, .demand = r.demand}));
  }
  sorted.sort().freeze();
  out.heading("EntityVector sorted by its key, and found by it");
  for (const auto ref : sorted.refs()) {
    out.line(std::format("{} {}", ref->name, sorted.find(ref->name) == ref));
  }
}

void model(const planning::Instance& data, Text& out) {
  IloEnv env;
  const IloModel model{env};
  const planning::concert::Model m{env, data};
  m.build(env, model);
  const auto variables = [&out](std::string_view name, const auto& table) {
    out.heading(std::format("variables {} ({})", name, table.size()));
    for (const auto& [row, var] : table) {
      const IloNumVarI* v = var.getImpl();
      out.line(std::format("{} [{}, {}] {}", v->name, v->lb, v->ub, static_cast<int>(v->type)));
    }
  };
  variables("Yield", m.vYield);
  variables("Setup", m.vSetup);
  variables("Stock", m.vStock);
  out.heading(std::format("rows ({})", model.cs_->size()));
  for (const IloConstraintI* c : *model.cs_) {
    std::string terms;
    for (const auto& [v, k] : c->terms) {
      terms += std::format(" {}*{}", k, v->name);
    }
    out.line(std::format("{} {} {}:{}", c->name.empty() ? "-" : c->name, static_cast<int>(c->sense), c->rhs, terms));
  }
  const auto holding_term = [&m](cItem i, cPeriod t) { return i->hold * m.vStock(i, t); };
  const auto setup_term   = [&m](cMachine k, cItem i, cPeriod t) { return k->setup * m.vSetup(k, i, t); };
  for (const auto& [name, sum] : {
           std::pair{"holding", dreamor::concert::sumExpr(env, m.sSTOCK, holding_term).build()},
           std::pair{"setup", dreamor::concert::sumExpr(env, m.sPRODUCE, setup_term).build()},
       }) {
    std::string terms;
    for (const auto& [v, k] : sum.expr_->terms) {
      terms += std::format(" {}*{}", k, v->name);
    }
    out.line(std::format("cost {} {}:{}", name, sum.expr_->constant, terms));
  }
  env.end();
}

}  // namespace

int main() try {
  const auto expected           = std::filesystem::path{__FILE__}.replace_filename("determinism.txt");
  const planning::Instance data = determinism::instance();
  for (const std::uint64_t seed : determinism::kHeapSeeds) {
    const determinism::ShakenHeap shaken{seed};
    Text out;
    setLayer(data, out);
    model(data, out);
    determinism::expectText(out.text(), expected, seed);
  }
  return report("determinism");
} catch (...) { return uncaught(); }
