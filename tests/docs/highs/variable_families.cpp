// SPDX-License-Identifier: MIT-0
//
// docs/guide/variable-families.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "docs/highs/plan.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/highs.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;
using namespace dreamor::highs;

// --8<-- [start:overtime-record]
struct OvertimeRecord {  // the extra hours a machine may work in a period, as agreed with its crew
  std::string machine;
  double hours;
};

// --8<-- [end:overtime-record]

HighsOptions quietOptions() {
  HighsOptions quiet;
  quiet.output_flag = false;
  return quiet;
}

// A column's bounds in the model HiGHS holds.
bool boundsAre(const HighsEnv& env, HighsInt column, double lower, double upper) {
  const HighsLp& lp = env.highs().getLp();
  const auto at     = static_cast<std::size_t>(column);
  return exactlyEqual(lp.col_lower_[at], lower) && exactlyEqual(lp.col_upper_[at], upper);
}

bool isInteger(const HighsEnv& env, HighsInt column) {
  const HighsLp& lp = env.highs().getLp();
  return lp.integrality_.size() == static_cast<std::size_t>(lp.num_col_) &&
         lp.integrality_[static_cast<std::size_t>(column)] == HighsVarType::kInteger;
}

void makeFamilies(const Plant& p, const Sets& s, const highs::Plan& plan) {
  const auto m1    = p.machines.at("M1");
  const auto m2    = p.machines.at("M2");
  const auto frame = p.items.at("frame");
  const auto plate = p.items.at("plate");
  const auto bolt  = p.items.at("bolt");

  // produce: M1's frame and plate, M2's plate, bolt and nut, in 3 periods; stock: 4 needed items in 3 periods.
  DREAMOR_CHECK(plan.vMake.size() == 15 && plan.vSetup.size() == 15 && plan.vStock.size() == 12);
  DREAMOR_CHECK(plan.vMake.keys() == s.produce && plan.vSetup.keys() == s.produce && plan.vStock.keys() == s.stock);
  DREAMOR_CHECK(plan.env.highs().getLp().num_col_ == 42);  // 15 + 15 + 12

  // vMake: continuous, from 0 to max_make - M1 works 4 hours, a frame takes 1 and a plate 0.5; M2 works 3 hours.
  DREAMOR_CHECK(!isInteger(plan.env, plan.vMake(m1, frame, 1).index()));
  DREAMOR_CHECK(boundsAre(plan.env, plan.vMake(m1, frame, 1).index(), 0, 4));
  DREAMOR_CHECK(boundsAre(plan.env, plan.vMake(m1, plate, 2).index(), 0, 8));
  DREAMOR_CHECK(boundsAre(plan.env, plan.vMake(m2, plate, 3).index(), 0, 6));
  // vSetup: integer, 0 or 1.
  DREAMOR_CHECK(isInteger(plan.env, plan.vSetup(m2, bolt, 2).index()));
  DREAMOR_CHECK(boundsAre(plan.env, plan.vSetup(m2, bolt, 2).index(), 0, 1));
  // vStock: continuous, from 0 with no upper bound.
  DREAMOR_CHECK(!isInteger(plan.env, plan.vStock(frame, 3).index()));
  DREAMOR_CHECK(boundsAre(plan.env, plan.vStock(frame, 3).index(), 0, kHighsInf));

  // A VarTable has no default constructor: a class that holds one makes it in its initializer list.
  static_assert(!std::is_default_constructible_v<VarTable<cMachine, cItem, cPeriod>>);
  static_assert(!std::is_default_constructible_v<VarTable<cItem, cPeriod>>);

  // Every table's variables belong to the environment declared before them.
  DREAMOR_CHECK(&plan.vMake(m1, frame, 1).env() == &plan.env && &plan.vSetup(m2, bolt, 2).env() == &plan.env &&
                &plan.vStock(frame, 3).env() == &plan.env);
}

void boundFromData(const Plant& p, const Sets& s) {
  const dreamor::EntityVector<Machine>& machines = p.machines;
  HighsEnv env{quietOptions()};

  // --8<-- [start:bound-from-data]
  using dreamor::index_set::relational::from;
  const auto sSHIFTS = from<cMachine>(machines).cross(s.periods).build();  // each machine in each period

  const std::vector<OvertimeRecord> agreed = {{"M1", 2.0}, {"M2", 1.0}};
  const auto machine_of = [&machines](const OvertimeRecord& r) { return cMachine{machines.at(r.machine)}; };
  const auto hours_of   = [](const OvertimeRecord& r) { return r.hours; };
  const dreamor::ParamTable<double, cMachine> overtime{agreed, machine_of, hours_of};

  const VarTable<cMachine, cPeriod> vOvertime{env, sSHIFTS, 0, overtime, HighsVarType::kContinuous, "Overtime"};
  // --8<-- [end:bound-from-data]
  // A ParamTable is a callable over its key columns: overtime reads the machine of a row of sSHIFTS, as the bounds
  // below show.
  DREAMOR_CHECK(overtime.size() == 2 && exactlyEqual(overtime(machines.at("M1")), 2.0) &&
                exactlyEqual(overtime(machines.at("M2")), 1.0));
  DREAMOR_CHECK(vOvertime.size() == 6);  // 2 machines in 3 periods
  std::size_t asAgreed = 0;
  for (const auto& [m, t] : sSHIFTS) {
    const double agreedHours = m == machines.at("M1") ? 2.0 : 1.0;  // the records above
    if (boundsAre(env, vOvertime(m, t).index(), 0, agreedHours)) { ++asAgreed; }
  }
  DREAMOR_CHECK(asAgreed == 6);  // M1's 3 periods up to 2 hours, M2's 3 up to 1
}

void read(const Plant& p, const Sets& s, const highs::Plan& plan) {
  const auto m1    = p.machines.at("M1");
  const auto m2    = p.machines.at("M2");
  const auto frame = p.items.at("frame");
  const auto nut   = p.items.at("nut");

  // --8<-- [start:read]
  const HighsVar frames   = plan.vMake(m1, frame, 1);  // M1's frames in period 1
  const std::string& name = plan.env.highs().getLp().col_names_[frames.index()];

  const bool makesNuts    = plan.vMake.contains(m1, nut, 1);  // false; vMake(m1, nut, 1) throws std::out_of_range
  const std::size_t count = plan.vMake.size();
  const auto& sMADE       = plan.vMake.keys();  // the set the family was made over
  // --8<-- [end:read]
  DREAMOR_CHECK(name == "Make_M1_frame_1");
  DREAMOR_CHECK(!makesNuts && plan.vMake.contains(m2, nut, 1));
  DREAMOR_CHECK_THROWS(std::out_of_range, plan.vMake(m1, nut, 1));
  DREAMOR_CHECK(count == 15);  // 5 machine and item pairs in 3 periods
  DREAMOR_CHECK(sMADE == s.produce);
}

void walk(const Plant& p, const Sets& s, const highs::Plan& plan) {
  // --8<-- [start:walk]
  std::vector<std::string> columns;
  for (const auto& [row, stock] : plan.vStock) {
    const auto& [i, t] = row;
    columns.push_back(std::format("{} in period {}: column {}", i, t, stock.index()));
  }
  // --8<-- [end:walk]
  DREAMOR_CHECK(columns.size() == 12);
  // stock holds the needed items in the order sNEEDED has them - frame and nut sold, then plate and bolt below -
  // each in periods 1 to 3; its 12 columns come after vMake's 15 and vSetup's 15.
  DREAMOR_CHECK(columns.front() == "frame in period 1: column 30");
  DREAMOR_CHECK(columns.back() == "bolt in period 3: column 41");
  // Each row's column is the next: the k-th row of the set has column 30 + k.
  HighsInt expected = 30;
  bool inOrder      = true;
  for (const auto& row : s.stock) {
    inOrder = inOrder && plan.vStock(row).index() == expected;
    ++expected;
  }
  // The items in sNEEDED's order: frame 30 to 32, nut 33 to 35, plate 36 to 38, bolt 39 to 41.
  DREAMOR_CHECK(inOrder && plan.vStock(p.items.at("nut"), 1).index() == 33);
  DREAMOR_CHECK(plan.vStock(p.items.at("plate"), 1).index() == 36 && plan.vStock(p.items.at("bolt"), 1).index() == 39);
}

void changeBound(const Plant& p, const Sets& s, const BomTable& quantity) {
  highs::Plan plan{s, quantity, quietOptions()};
  const dreamor::Ref<Machine> m2 = p.machines.at("M2");
  const dreamor::Ref<Item> plate = p.items.at("plate");

  // --8<-- [start:change-bound]
  // M2's plate tooling is away
  const auto lacks_tooling = [m2, plate](cMachine m, cItem i) { return m == m2 && i == plate; };

  for (const auto& row : s.produce.where(lacks_tooling)) {
    plan.env.highs().changeColBounds(plan.vMake(row).index(), 0, 0);
  }
  plan.env.highs().run();
  // --8<-- [end:change-bound]
  // M2's plate, in periods 1, 2 and 3: three columns fixed to 0, and no other.
  std::size_t fixed = 0;
  for (const auto& [row, make] : plan.vMake) {
    if (boundsAre(plan.env, make.index(), 0, 0)) { ++fixed; }
  }
  DREAMOR_CHECK(fixed == 3);
  DREAMOR_CHECK(boundsAre(plan.env, plan.vMake(m2, plate, 1).index(), 0, 0) &&
                boundsAre(plan.env, plan.vMake(m2, plate, 2).index(), 0, 0) &&
                boundsAre(plan.env, plan.vMake(m2, plate, 3).index(), 0, 0));
  DREAMOR_CHECK(boundsAre(plan.env, plan.vMake(p.machines.at("M1"), plate, 1).index(), 0, 8));  // M1's as it was

  // The next run makes every plate on M1: M1 has 4 hours a period for 5 frames and their 10 plates, 10 hours.
  DREAMOR_CHECK(plan.env.highs().getModelStatus() == HighsModelStatus::kOptimal);
  const std::vector<double>& made = plan.env.highs().getSolution().col_value;
  DREAMOR_CHECK(isExactlyZero(made[static_cast<std::size_t>(plan.vMake(m2, plate, 1).index())]) &&
                isExactlyZero(made[static_cast<std::size_t>(plan.vMake(m2, plate, 2).index())]) &&
                isExactlyZero(made[static_cast<std::size_t>(plan.vMake(m2, plate, 3).index())]));
}

}  // namespace

int main() try {
  const Plant p           = plant();
  const BomTable quantity = bomTable(p);
  const Sets s            = byChain(p, quantity);
  const highs::Plan plan{s, quantity, quietOptions()};
  makeFamilies(p, s, plan);
  boundFromData(p, s);
  read(p, s, plan);
  walk(p, s, plan);
  changeBound(p, s, quantity);
  return ::report("guide/variable-families");
} catch (...) { return ::uncaught(); }
